# SafeWave-AI ESP32-S3 CSI 펌웨어

ESP32-S3가 WiFi CSI(Channel State Information)를 수집·전처리하여 **788바이트 고정 UDP 패킷**을
라즈베리파이 `sensing` 서비스로 **100 Hz** 송신하는 센서 노드 펌웨어입니다.
대역분리(Butterworth 2밴드)까지 펌웨어가 처리하며, RPi는 받은 데이터를 그대로 추론에 사용합니다.

## 주요 기능

- WiFi CSI 콜백에서 64채널 amplitude `√(I²+Q²)` 추출
- per-frame peak 정규화 → `block_raw` (광대역)
- 64채널 IIR Butterworth 4차 (채널별 상태 유지)
  - `block_resp` : 호흡 0.1–0.6 Hz
  - `block_heart`: 심박 0.8–3.0 Hz
- 788B 패킷 패킹 후 UDP 송신 (100 Hz)
- 게이트웨이 더미 트래픽으로 CSI 콜백 cadence 유지
- SNTP 시각 동기, 10초 주기 rate/drop/rssi 진단 로그

## 하드웨어

- ESP32-S3 (N16R8 — 16 MB Flash / 8 MB Octal PSRAM)
- 2.4 GHz WiFi AP, RPi sensing 호스트

## 빌드 & 플래시

### 1. 필터 계수 (이미 생성·커밋됨)

`src/biquad_coeffs.h`에 Butterworth 계수가 이미 들어 있어 **바로 빌드됩니다.**
필터 대역을 바꿀 때만 재생성하세요 (파일 인자 형식 — Windows/PowerShell 안전):

```bash
pip install scipy numpy
python tools/gen_sos.py src/biquad_coeffs.h
```

### 2. 설정 편집

`src/config.h` 상단의 값을 환경에 맞게 수정합니다.

```c
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASS"
#define TARGET_IP     "192.168.1.100"   // RPi sensing 호스트
#define CSI_FS        100               // RPi CSI_FS / M2 학습 fs와 일치 필수
```

> `NODE_ID`는 `config.h`에서 손대지 않습니다. 빌드 env(`node1`~`node6`)가
> `-DNODE_ID=n`으로 주입합니다 (아래 참조).

### 3. 빌드 및 업로드 (노드별)

노드 6개는 각각 `node1`~`node6` env로 빌드합니다. **COM 번호는 NODE_ID와 무관**하며,
그 보드가 USB에 꽂힌 포트일 뿐입니다.

```bash
pio device list                                  # 보드의 COM 포트 확인
pio run -e node3 -t upload --upload-port COM7     # "3번" 보드 굽기
pio device monitor -e node3 --port COM7           # 시리얼 로그 (115200)
```

보드 한 개씩: `포트 확인 → 노드 번호 결정 → 해당 env로 업로드 → 보드에 번호 라벨`.
`pio run` (env 생략)은 6개 전부 빌드합니다.

## 설정 (config.h)

| 항목 | 기본값 | 설명 |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | — | AP 접속 정보 |
| `TARGET_IP` / `TARGET_PORT` | `192.168.1.100` / `5005` | RPi sensing 목적지 |
| `NODE_ID` | `1` | 노드 번호 1~6 — 빌드 env가 `-DNODE_ID`로 주입 (config.h 수정 X) |
| `CSI_FS` | `100` | 샘플링 레이트(Hz) — RPi·M2와 3자 일치 |
| `CSI_N_CH` | `64` | 서브캐리어 채널 수 |

> `CSI_FS`는 `ESP 송신 rate = RPi CSI_FS = M2 학습 fs` 셋이 반드시 같아야 합니다.
> 다르면 호흡·심박 주파수 추출이 틀어집니다.

## 패킷 포맷 (788바이트, little-endian)

Python 대응: `struct.Struct("<4sBBHIIhH192f")`

| Offset | 필드 | 타입 | Bytes | 설명 |
|---:|---|---|---:|---|
| 0 | `magic` | `char[4]` | 4 | `"CSI!"` |
| 4 | `node_id` | `uint8` | 1 | 1~6 |
| 5 | `reserved` | `uint8` | 1 | 0 |
| 6 | `n_samples` | `uint16` | 2 | 64 |
| 8 | `seq_num` | `uint32` | 4 | 송신 카운터 |
| 12 | `ts_ms` | `uint32` | 4 | Unix ms 하위 32비트 |
| 16 | `rssi` | `int16` | 2 | dBm |
| 18 | `reserved2` | `uint16` | 2 | 0 |
| 20 | `block_raw[64]` | `float32[64]` | 256 | 정규화 amplitude (광대역) |
| 276 | `block_resp[64]` | `float32[64]` | 256 | 0.1–0.6 Hz 필터 |
| 532 | `block_heart[64]` | `float32[64]` | 256 | 0.8–3.0 Hz 필터 |

## 필터

| 밴드 | 대역 | 차수 | fs |
|---|---|---|---|
| 호흡 | 0.1–0.6 Hz | Butterworth 4차 | 100 Hz |
| 심박 | 0.8–3.0 Hz | Butterworth 4차 | 100 Hz |

계수는 `tools/gen_sos.py`(scipy)로 오프라인 생성하여 `src/biquad_coeffs.h`에 넣습니다.
대역을 바꾸려면 스크립트의 파라미터만 수정 후 재생성하면 됩니다.

## 프로젝트 구조

```
├── platformio.ini          PlatformIO 프로젝트 (framework = espidf)
├── sdkconfig.defaults      PSRAM / WiFi CSI / 1ms tick 설정
├── src/
│   ├── config.h            전 상수 (NODE_ID, SSID, IP, CSI_FS)
│   ├── packet.h            BinaryPacket + packet_fill()
│   ├── biquad.h / .c       SOS biquad cascade, 채널별 상태
│   ├── biquad_coeffs.h     gen_sos.py 출력 (계수)
│   ├── csi_capture.h / .c  CSI 콜백, amplitude 추출
│   ├── net.h / .c          UDP 송신, 더미 트리거, 100Hz 태스크
│   └── main.c              부팅, WiFi, SNTP, 초기화
└── tools/
    └── gen_sos.py          Butterworth SOS 계수 생성기
```

## 진단

시리얼 로그에 10초마다 출력됩니다.

```
rate=99.8Hz  seq=12345  drops=3(0.0%)  rssi=-58dBm
```

`config.h`의 `// #define CONFIG_APP_DEBUG_CSI` 주석을 해제하면
첫 채널의 `raw/resp/heart` 샘플 값이 함께 덤프되어 대역 동작을 확인할 수 있습니다.
