# SafeWave-AI — ESP32-S3 CSI 센서 노드 펌웨어

독거인 안전 모니터링(SafeWave-AI)의 **센서 노드 펌웨어**입니다.
ESP32-S3가 WiFi **CSI(Channel State Information)**를 수집·전처리하여
**788바이트 고정 UDP 패킷**을 라즈베리파이(RPi) `sensing` 서비스(`:5005`)로 **100 Hz**로 송신합니다.

> **방식 B (Method B)** — 대역분리(Butterworth 2밴드)까지 **펌웨어가 담당**합니다.
> RPi 쪽에는 `scipy` 필터가 전혀 없습니다. RPi는 받은 데이터를 그대로 reshape → ONNX 추론에만 사용합니다.

---

## 1. 데이터 흐름

```
[ESP32-S3 노드 1~6]
   │  CSI 콜백(esp_wifi_set_csi_rx_cb) → amplitude √(I²+Q²) 64채널
   │  per-frame peak 정규화 → block_raw (0~1, 광대역)
   │  64ch IIR Butterworth(상태 유지)
   │     ├─ 0.1–0.6 Hz → block_resp  (호흡)
   │     └─ 0.8–3.0 Hz → block_heart (심박)
   │  788B 패킹
   └──── UDP :5005 @100Hz ───▶ [RPi sensing/main.py] → Redis csi:raw (data_raw/resp/heart)
                                                          → M1 fall / M2 vital ONNX
```

- CSI 콜백은 **RX 시에만** 발생하므로, 게이트웨이로 10 ms 주기 **더미 UDP**를 쏴 트래픽을 유발해 콜백을 강제합니다.
- 콜백은 최신 amplitude 프레임을 **공유 버퍼에 저장만** 하고, 별도 **100 Hz 송신 태스크**가 최신 프레임을 꺼내 `필터 → 패킹 → 송신`합니다.
- 100 Hz 미만 취득 시 **최신 프레임 재사용 + drop 카운트 로그**.

---

## 2. 와이어 계약 (절대 변경 금지)

`src/packet.h` · little-endian · **788바이트 고정** · Python `struct.Struct("<4sBBHIIhH192f")`

| Offset | 필드 | C 타입 | Bytes | 설명 |
|---:|---|---|---:|---|
| 0 | `magic[4]` | `char[4]` | 4 | `"CSI!"` — 불일치 시 RPi가 패킷 폐기 |
| 4 | `node_id` | `uint8` | 1 | 1~6 (`NODE_ID`) |
| 5 | `reserved` | `uint8` | 1 | 0 |
| 6 | `n_samples` | `uint16` | 2 | 64 고정 |
| 8 | `seq_num` | `uint32` | 4 | 단조 증가 카운터 (랩어라운드 무시) |
| 12 | `ts_ms` | `uint32` | 4 | Unix ms 하위 32비트 |
| 16 | `rssi` | `int16` | 2 | dBm |
| 18 | `reserved2` | `uint16` | 2 | 0 |
| 20 | `block_raw[64]` | `float32[64]` | 256 | **M1** 정규화 amplitude (광대역, 필터 X) |
| 276 | `block_resp[64]` | `float32[64]` | 256 | **M2** 0.1–0.6 Hz Butterworth |
| 532 | `block_heart[64]` | `float32[64]` | 256 | **M2** 0.8–3.0 Hz Butterworth |

`static_assert(sizeof(BinaryPacket) == 788)` 로 컴파일 타임 검증됩니다.
`block_resp`/`block_heart`는 `block_raw`와 **동일한 64-amplitude 프레임**에 각각 대역 IIR을 적용한 결과입니다.

---

## 3. 파일 구조

```
safewave-ai-ambient-monitoring-firmware/
├── platformio.ini          PlatformIO 프로젝트 (framework = espidf)
├── sdkconfig.defaults      PSRAM·WiFi CSI·1ms tick 활성화
├── src/
│   ├── config.h            ★ 모든 상수 (NODE_ID, SSID/PW/IP, CSI_FS) — 플래시 전 편집
│   ├── packet.h            BinaryPacket + static_assert(788) + packet_fill()
│   ├── biquad.h / .c       DF-II Transposed SOS cascade, 채널별 상태 유지
│   ├── biquad_coeffs.h     ⚠ gen_sos.py 출력 (현재 placeholder 0 — 반드시 생성)
│   ├── csi_capture.h / .c  CSI 콜백, amplitude 추출, 공유 프레임 버퍼
│   ├── net.h / .c          UDP 송신 + 더미 트리거 + 100Hz 송신 태스크
│   └── main.c              부팅, WiFi(STA), SNTP, 초기화 시퀀스
└── tools/
    └── gen_sos.py          scipy Butterworth SOS 계수 생성기
```

---

## 4. 빌드 & 플래시

### 4-0. ⚠ 필수 선행: 필터 계수 생성

`src/biquad_coeffs.h`는 **placeholder(전부 0)** 상태로 커밋되어 있습니다.
이대로 빌드하면 **필터 출력이 0**입니다. 빌드 전 반드시 실행하세요.

```bash
pip install scipy numpy
python tools/gen_sos.py > src/biquad_coeffs.h
```

### 4-1. config.h TODO 편집

`src/config.h` 상단의 placeholder를 환경에 맞게 수정:

```c
#define WIFI_SSID     "YOUR_SSID"      // TODO
#define WIFI_PASSWORD "YOUR_PASS"      // TODO
#define TARGET_IP     "192.168.1.100"  // TODO: RPi sensing 호스트
#define NODE_ID       1                // TODO: 디바이스마다 1~6
```

### 4-2. 빌드/업로드 (PlatformIO)

```bash
pio run -t upload      # 빌드 + 플래시
pio device monitor     # 115200 시리얼 로그
```

> **참고:** 이 프로젝트는 PlatformIO(`framework = espidf`) 레이아웃(`src/`)을 사용합니다.
> 명세상의 `idf.py build`(`main/` + 루트 `CMakeLists.txt`) 구조와는 다릅니다.
> 순수 ESP-IDF `idf.py` 빌드가 필요하면 `main/CMakeLists.txt`와 루트 `CMakeLists.txt`를
> 추가해야 합니다 (요청 시 제공 가능).

---

## 5. 필터 상수 (1차값 — 교체 가능)

| 밴드 | 대역 | 차수 | fs |
|---|---|---|---|
| 호흡(resp) | 0.1–0.6 Hz | Butterworth 4차 | 100 Hz |
| 심박(heart) | 0.8–3.0 Hz | Butterworth 4차 | 100 Hz |

- 4차 bandpass = **2-section biquad cascade** (scipy `sos` → 4 sections), DF-II Transposed.
- 채널별 상태 유지: `64ch × 2band × 4section × 2delay`.
- 기준: PulseFi (arXiv:2510.24744). 검증 후 `tools/gen_sos.py`의 대역만 바꿔 재생성하면 됩니다.
  **와이어 포맷·Redis 스키마는 필터 교체와 무관하게 불변.**

### ⚠ `CSI_FS = 100` 3자 일치 (치명적 제약)

```
ESP 송신 rate  ==  RPi CSI_FS 환경변수  ==  M2 ONNX 학습 fs
```

하나라도 다르면 0.1–3 Hz 미세 주기(호흡·심박) 추출이 틀어집니다.
`src/config.h`의 `#define CSI_FS 100` **한 곳**에서만 관리하세요.

---

## 6. 실측 검증이 필요한 가정 (HW 필요)

코드 내 주석으로도 표기되어 있으나, ESP32-S3 실기에서 **반드시 확인**할 항목:

| # | 항목 | 검증 방법 | 위치 |
|---|---|---|---|
| 1 | 패킷 788B | `static_assert` 통과 | `packet.h` |
| 2 | `magic "CSI!"` 통과 | RPi rx 증가, error=0 | RPi 측 |
| 3 | **fs 3자 일치** | ESP rate = `CSI_FS` = M2 학습 fs | `config.h` |
| 4 | **CSI 서브캐리어 매핑** | 부팅 시 `info->len` 출력 → 128(64×2) 기대, non-zero 인덱스 확인 | `csi_capture.c` |
| 5 | resp/heart 대역 | `block_resp` FFT 피크가 0.1–0.6 Hz | 디버그 덤프 |
| 6 | raw 광대역 보존 | `block_raw`에 움직임 변화 보존 | 디버그 덤프 |
| 7 | **100Hz 실측 rate** | 더미 트래픽으로 콜백 유발, drop < 1% | `net.c` 10초 로그 |
| 8 | PSRAM 안정성 | 장시간 OOM 없음 | `biquad_init` 로그 |

**가장 불확실한 두 가지** (실측 전까지 가정):
- **(4) CSI 서브캐리어 매핑**: HT20 LLTF가 `buf`를 int8 I/Q 64쌍으로 채운다고 가정. 64개 미만 도착 시 **제로패딩** 규칙(`csi_capture.c` 주석 참조). 실기에서 `info->len`과 non-zero 분포 확인 후 매핑 주석 갱신 필요.
- **(7) 100Hz 트래픽 유발**: 더미 UDP가 게이트웨이 TX를 깨워 CSI 콜백을 규칙적으로 발생시킨다는 가정. AP/환경에 따라 콜백 rate가 100Hz에 못 미칠 수 있음 → 그 경우 최신 프레임 재사용(로그 경고).

디버그 덤프(5·6)는 `config.h`의 `// #define CONFIG_APP_DEBUG_CSI` 주석을 해제하면 첫 채널 샘플이 10초마다 출력됩니다.

---

## 7. 왜 C++(쁠쁠)가 아니라 C 인가?

`db_spec` 명세가 C `struct __attribute__((packed))`를 **와이어 계약**으로 못박았고, 그대로 가는 게 가장 안전합니다. 구체적으로:

- **ESP-IDF 기본 언어가 C** — `esp_wifi_set_csi_rx_cb`, FreeRTOS, lwIP 등 핵심 API가 모두 C. C로 쓰면 `extern "C"` 래핑/이름 맹글링 걱정이 없습니다.
- **`packed struct` = 와이어 포맷.** C에서는 메모리 레이아웃이 곧 788B 바이트열이라 `memcpy`/`sendto`가 그대로 통합니다. C++의 클래스/가상함수/생성자는 레이아웃에 숨은 바이트(vtable, padding)를 끼워 넣을 위험이 있어 `static_assert(==788)`를 더 까다롭게 만듭니다.
- **결정적·저오버헤드.** 100Hz × 64ch × 2밴드 IIR을 도는 실시간 루프라 예외/RTTI/동적할당 같은 C++ 런타임 기능이 불필요하고, 오히려 지연·메모리 변동의 원인이 됩니다.
- **명세 직역.** 명세의 C 코드를 1:1로 옮기면 리뷰·검증이 쉽고, RPi의 Python `struct` 포맷과 바이트 단위로 대응이 명확합니다.

> 요약: 와이어 계약이 packed C struct이고 타깃 SDK가 C 중심이므로, C가 추상화 비용 없이 바이트 레이아웃을 그대로 통제하는 가장 직접적인 선택입니다.

---

## 8. RPi 백엔드 (별도 레포)

이 펌웨어는 **센서 노드 전용**입니다. RPi 측 코드는 이 레포에 없습니다.
플래시 후 RPi 백엔드를 방식 B로 갱신해야 합니다 (`sensing/main.py`, `ai/main.py`,
`ai/experts/m1_wifi_pose.py`, `ai/experts/m2_frenel_vital.py` — `scipy` 필터 제거,
`csi:raw` 3-필드 `data_raw`/`data_resp`/`data_heart` 대응).
