# REFERENCE NOTES — 강조 사실 모음

펌웨어 작업 중 **반드시 기억해야 할 사실들**. 특히 §3(레퍼런스 펌웨어 대조)이 핵심.

---

## 1. 와이어 계약 (절대 불변)

788바이트 고정 · little-endian · UDP:5005 · Python `struct.Struct("<4sBBHIIhH192f")`

| Offset | 필드 | 타입 | Bytes |
|---:|---|---|---:|
| 0 | magic `"CSI!"` | char[4] | 4 |
| 4 | node_id (1~5) | uint8 | 1 |
| 5 | reserved | uint8 | 1 |
| 6 | n_samples (64) | uint16 | 2 |
| 8 | seq_num | uint32 | 4 |
| 12 | ts_ms | uint32 | 4 |
| 16 | rssi | int16 | 2 |
| 18 | reserved2 | uint16 | 2 |
| 20 | block_raw[64] | float32×64 | 256 |
| 276 | block_resp[64] (0.1–0.6Hz) | float32×64 | 256 |
| 532 | block_heart[64] (0.8–3.0Hz) | float32×64 | 256 |

`static_assert(sizeof(BinaryPacket)==788)` 로 컴파일 타임 강제. **이 포맷이 바뀌면 RPi `sensing` 파서도 같이 바뀌어야 함.**

## 2. ⚠️ `CSI_FS = 100` 3자 일치 (치명적)

```
ESP 송신 rate  ==  RPi CSI_FS 환경변수  ==  M2 ONNX 학습 fs
```
하나라도 어긋나면 호흡(0.1–0.6Hz)·심박(0.8–3.0Hz) 주파수 추출이 통째로 틀어짐. `config.h`의 `#define CSI_FS 100` 한 곳에서만 관리.

---

## 3. ★ 레퍼런스 펌웨어 대조 (가장 중요)

> 출처: `github.com/composedly13/AI_HACK_CAMP_2026_CSI` **`firmware` 브랜치**
> (`src/csi_collector.cpp`, `src/main.cpp`, `platformio.ini`)

이건 **Method A 시절의 실제 동작 ESP32-S3 수집기**다. 설계는 우리(v2/Method B)와 다르지만, **하드웨어 레벨 CSI 취득은 동일**해서 우리 가정을 실코드로 검증해준다.

### 레퍼런스 vs 우리

| 항목 | 레퍼런스 (firmware 브랜치) | 우리 (v2/Method B) |
|---|---|---|
| 프레임워크 | **Arduino** | ESP-IDF |
| 전송 | **HTTP POST** (`/csi/log`) | **UDP :5005** |
| cadence | **20 Hz** (`<50ms`) | **100 Hz** (`<10ms`) |
| 패킷 | `<4s8sIIi64f>` = **280B**, 단일 64f | `<4sBBHIIhH192f>` = **788B**, 3블록 |
| node_id | 8바이트 **문자열** | uint8 (1~5) |
| rssi | int32 | int16 |
| 전처리 | **없음** (raw amplitude) | peak 정규화 + Butterworth 2밴드 |

### ✅ 우리 코드를 검증해주는 부분 (실동작으로 확인됨)
1. **amplitude 추출 100% 동일** — `pairs = info->len/2` → `sqrtf(I*I+Q*Q)` → 64개 cap → 나머지 zero-pad. 우리 `csi_capture.c`와 같은 로직. **서브캐리어 인덱스 리매핑은 안 하는 게 맞음**(둘 다 raw pairs 그대로).
2. **더미 UDP→게이트웨이로 CSI 콜백 유발** — 우리 `net.c`의 핵심 트릭과 동일. **검증된 기법.**
3. magic `"CSI!"` / 64 amplitude / N16R8 PSRAM 일치.

### ⚠️ 현장에서 챙길 차이 3가지
1. **cadence — 레퍼런스는 20Hz였다.** 주석에 `// 20 Hz` 명시. 그들이 20Hz를 택했다는 건 **100Hz가 빡빡할 수 있다는 실전 신호.**
   - 현장 체크 #7에서 `rate`가 100Hz 안 나오면 이게 원인.
   - 단 우리 `CSI_FS=100`은 M2 학습 fs와 묶여 못 낮춤 → 안 나오면 **백엔드팀과 재논의** 사안 (fs 재학습 or AP/환경 튜닝).
2. **CSI config 차이:**
   - 레퍼런스: `lltf_en + htltf_en + stbc_htltf2_en + ltf_merge_en` 전부 on
   - 우리: `lltf_en`만 on
   - 우리가 LLTF-only라 **프레임마다 서브캐리어가 더 일정** → M2 대역필터엔 오히려 유리. 대신 `info->len`이 둘이 다르게 나올 수 있어 **부팅 시 `info->len` 실측이 더 중요.**
3. **레퍼런스는 WiFi 채널을 13으로 고정**(`esp_wifi_set_channel(13, ...)`). 우리는 STA로 AP 채널을 따라감 → 보통 무방하나, **CSI가 안 잡히면 채널/AP** 의심.

### 결론
- CSI가 안 잡혀도 **amplitude 추출·더미 트리거는 의심 대상 아님**(실코드 검증됨) → `info->len`/cadence/채널을 봐라.
- **100Hz는 20Hz 선례 대비 공격적** → 현장 1순위 관찰 포인트.

---

## 4. 현장에서만 검증 가능한 가정 (HW 필요)

| # | 가정 | 확인 방법 |
|---|---|---|
| 1 | CSI 서브캐리어 = 64 | 부팅 시 `info->len` 출력 → **128**(64×2) 기대, non-zero 분포 확인 |
| 2 | 100Hz cadence | 10초 로그 `rate≈100Hz`, `drops<1%` (레퍼런스는 20Hz였음 — 주의) |
| 3 | PSRAM 안정성 | `biquad_init` 로그, 장시간 OOM 없음 |
| 4 | resp/heart 대역 | `block_resp` FFT 피크가 0.1–0.6Hz (디버그 덤프) |

---

## 5. 빌드/플래시 빠른 참조

```bash
# (계수는 이미 커밋됨 — 대역 바꿀 때만)
python tools/gen_sos.py src/biquad_coeffs.h

# 노드별 (COM은 NODE_ID와 무관)
pio device list
pio run -e node3 -t upload --upload-port COM7
pio device monitor -e node3 --port COM7
```

N16R8: 16MB flash + 8MB Octal PSRAM + 1.5MB(`partitions_singleapp_large.csv`) 파티션.
검증된 사용량: RAM 12.1% / Flash 51.1%.
