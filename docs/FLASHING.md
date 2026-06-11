# FLASHING — 펌웨어 굽는 절차 명세

ESP32-S3-N16R8 6노드 현장 플래시 절차. 위에서 아래로 순서대로 따라가면 된다.

> 전제: 코드/계수는 이미 빌드 가능 상태로 커밋됨. **현장에서 새로 짤 코드는 없다.**
> WiFi 정보 입력 → 노드별 굽기 → 시리얼 로그 확인, 이 3단계가 전부.

---

## 0. 준비물

| 항목 | 확인 |
|---|---|
| ESP32-S3-N16R8 보드 ×6 | OTG/데이터용 USB 케이블 (충전 전용 X) |
| PC 도구 | Python 3.11, PlatformIO 6.x, VS Code — 이미 설치됨 |
| USB 드라이버 | CP210x 또는 CH343 (보드 USB-UART 칩에 맞게) |
| AP/네트워크 | 2.4GHz WiFi, RPi `sensing` 호스트 IP |

도구 확인:
```powershell
pio --version          # PlatformIO Core 6.x
python --version       # 3.11.x
```

---

## 1. (선택) 필터 계수 재생성

`src/biquad_coeffs.h`는 **이미 실계수가 커밋**돼 있어 **생략 가능**.
필터 대역을 바꿀 때만:
```powershell
pip install scipy numpy
python tools/gen_sos.py src/biquad_coeffs.h    # 파일 인자 형식 (PowerShell `>` 금지: UTF-16 깨짐)
```

---

## 2. WiFi/네트워크 설정 (`src/config.h`)

```c
#define WIFI_SSID     "실제_AP_이름"
#define WIFI_PASSWORD "실제_비번"
#define TARGET_IP     "192.168.x.x"   // RPi sensing 호스트
// CSI_FS 100 은 그대로 (RPi/M2와 3자 일치 — 건드리지 말 것)
```

> `NODE_ID`는 여기서 **안 건드린다.** 빌드 env(`node1`~`node6`)가 자동 주입.

---

## 3. 보드 연결 & COM 포트 확인

보드 **한 개만** 꽂고:
```powershell
pio device list
```
→ `COM7` 같은 포트가 보이면 인식 성공.

- 안 보이면: 케이블(데이터용?)·드라이버(CP210x/CH343)·BOOT 버튼 확인
- **COM 번호 ≠ 노드 번호.** 그냥 지금 이 보드가 꽂힌 포트일 뿐.

---

## 4. 노드별 굽기 (핵심)

보드 1개씩, "이 보드 = N번"을 정하고 해당 env로 업로드한다.

```powershell
# 예: 이 보드를 3번 노드로
pio run -e node3 -t upload --upload-port COM7
```

성공하면 **보드에 "3" 라벨**을 붙이고 다음 보드로.

| 보드 | env | 명령 |
|---|---|---|
| 1번 | node1 | `pio run -e node1 -t upload --upload-port COMx` |
| 2번 | node2 | `pio run -e node2 -t upload --upload-port COMx` |
| … | … | … |
| 6번 | node6 | `pio run -e node6 -t upload --upload-port COMx` |

> **주의:** 같은 NODE_ID로 두 보드를 굽지 말 것. env를 다르게 = NODE_ID가 다르게.
> 펌웨어 바이너리는 NODE_ID만 다르므로 빌드는 빠름(프레임워크 캐시).

업로드 안 될 때:
- BOOT 누른 채 RESET → 다운로드 모드 진입 후 재시도
- `--upload-port` 명시 (자동 인식 실패 시)
- `upload_speed` 낮추기 (`platformio.ini`에서 921600 → 460800)

---

## 5. 시리얼 로그로 검증

```powershell
pio device monitor -e node3 --port COM7      # 115200
```

부팅 순서대로 확인:

| # | 봐야 할 로그 | 기대값 | 안 되면 |
|---|---|---|---|
| 1 | `IP: 192.168...` | WiFi 접속 OK | SSID/PW, 2.4GHz 여부 |
| 2 | `CSI capture ready` / `info->len` | **128**(64×2) 기대 | §7 참조 |
| 3 | `rate=~100Hz  drops=..(<1%)` | 100Hz 근접 | §7 cadence |
| 4 | `rssi=-XXdBm` | 합리적 값(-40~-75) | AP 거리/채널 |

대역 동작까지 보려면 `config.h`의 `// #define CONFIG_APP_DEBUG_CSI` 주석 해제 후 재빌드 →
`raw[0]/resp[0]/heart[0]` 샘플이 10초마다 덤프됨.

---

## 6. RPi 수신 확인

펌웨어가 쏘기만 해선 끝이 아니다. RPi `sensing` 서비스가:
- **:5005 UDP 수신** 중
- **방식 B**(`csi:raw` 3-필드 `data_raw`/`data_resp`/`data_heart`)로 갱신돼 있어야 패킷이 소비됨

(RPi 코드는 별도 레포 — 이 펌웨어엔 없음)

---

## 7. 트러블슈팅 — CSI가 안 잡힐 때

> 레퍼런스(`AI_HACK_CAMP_2026_CSI` firmware 브랜치)로 **amplitude 추출·더미 트리거는 검증됨** →
> 거기 말고 아래를 의심. 상세는 [REFERENCE_NOTES.md](REFERENCE_NOTES.md) §3.

1. **`info->len`이 128이 아님** → 서브캐리어 매핑 가정과 다름. 실제 값 보고 `csi_capture.c` 주석/매핑 갱신.
2. **`rate`가 100Hz 미달** → 가장 흔함. 레퍼런스는 20Hz로 동작했음.
   - 더미 트리거 주기/우선순위 확인
   - 그래도 안 되면 `CSI_FS`는 M2와 묶여 못 낮추므로 **백엔드팀과 재논의**(fs 재학습 or AP 튜닝)
3. **CSI 콜백 자체가 안 옴** → WiFi 채널/AP 의심 (레퍼런스는 채널 13 고정). STA 모드라 AP 채널을 따라감.
4. **OOM / 재부팅 루프** → PSRAM(octal) 인식 실패 의심. `biquad_init` 로그 확인.

---

## 현장 체크리스트 (요약)

```
[ ] config.h: SSID / PW / TARGET_IP 입력 (CSI_FS=100 유지)
[ ] 보드별: pio device list → pio run -e nodeN -t upload --upload-port COMx → 라벨
[ ] 시리얼: IP / info->len=128 / rate≈100Hz / rssi 확인
[ ] RPi :5005(방식B) 수신 확인
[ ] 통과 시 git tag v0.1.0-field
```
