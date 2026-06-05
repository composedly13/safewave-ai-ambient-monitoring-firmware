# DEVLOG — SafeWave-AI ESP32-S3 CSI 펌웨어

펌웨어 작업 이력. 최신 항목이 위. 날짜는 절대표기(KST).

---

## 2026-06-05 · 빌드 정비 · 6노드 · N16R8 · CI

스캐폴드(`de749bb`)는 코드가 사실상 완성 상태였고, 이날 **"현장에서 굽기만 하면 되는 상태"** 로 다듬음.

### 1. `gen_sos.py` Windows 버그 수정 + 계수 생성
- **증상:** `python tools/gen_sos.py > src/biquad_coeffs.h` 실행 시
  - 주석의 대시 문자(`—`/`–`)가 cp949로 인코딩 안 돼 **크래시**
  - PowerShell `>` 리다이렉트가 **UTF-16**을 만들어 C 빌드가 깨질 위험
- **수정:** 출력을 **순수 ASCII** 로 바꾸고, **경로 인자**로 파일을 직접 쓰는 모드 추가
  - `python tools/gen_sos.py src/biquad_coeffs.h`  ← 권장 (크로스플랫폼 안전)
- `src/biquad_coeffs.h` 에 실제 Butterworth SOS 계수 생성·커밋 (resp 0.1–0.6Hz / heart 0.8–3.0Hz, fs=100, 4 sections). 이제 **clone 후 바로 빌드** 가능.

### 2. 6노드 빌드 분리
- `config.h`: `#define NODE_ID 1` → `#ifndef NODE_ID` 가드 (빌드 인자 우선)
- `platformio.ini`: 공통 `[env]` + `node1`~`node6`, 각 `-DNODE_ID=n` 주입
- **config.h를 손으로 6번 고칠 필요 없음** → 중복 NODE_ID 사고 방지
- 플래시: `pio run -e node3 -t upload --upload-port COM7`
- **COM 번호는 NODE_ID와 무관** (보드가 꽂힌 포트일 뿐)

### 3. N16R8 타깃 정비
- `board_upload.flash_size = 16MB` + `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` (부트로더 헤더 일치)
- 8MB Octal PSRAM: `CONFIG_SPIRAM_MODE_OCT` (sdkconfig)
- 파티션: `board_build.partitions = partitions_singleapp_large.csv` (factory **1.5MB**)
  - 기본 1MB 앱이 75% 차서 headroom 확보. PlatformIO 네이티브 지정으로 size-check도 정확해짐.

### 4. 문서/검증
- `README.md`: 빌드/플래시/검증 + 노드별 업로드 + 788B 패킷 포맷
- **빌드 검증:** 풀 `pio run` SUCCESS — `RAM 12.1%`, `Flash 51.1% (784,693 / 1,536,000)`
- 미검증(현장 필요): CSI 서브캐리어 매핑(`info->len`), 100Hz cadence → [REFERENCE_NOTES](REFERENCE_NOTES.md)

### 5. CI + Git 위생
- GitHub Actions `.github/workflows/build.yml`: push/PR마다 `pio run -e node1` → README 빌드 배지
- **PR 흐름:** feature 브랜치(`ci/add-build-workflow`) → push → `--no-ff` 머지 → 브랜치 삭제
- **태그 순서 교정:** 처음에 머지 전 main에 `v0.1.0`을 찍었다가 회수 → **머지 후** 재태깅 (릴리즈에 CI 포함)
- 태그 `v0.1.0` = 빌드 가능 첫 버전

### 커밋 맵
```
eec2589  Merge: GitHub Actions build CI + README badge   ← v0.1.0
64c2709  fix(build): 1.5MB 파티션 + README 정확성
f779d0d  chore(build): N16R8 + 6노드 env
95b3c23  (README)
de749bb  feat: initial scaffold (Method B)
```

---

## 다음 (현장)
1. `config.h`에 실제 WiFi(SSID/PW/RPi IP) 입력
2. 보드별 `pio run -e nodeN -t upload --upload-port COMx`
3. 시리얼로 (1) IP접속 (2) `info->len`==128 (3) `rate≈100Hz` (4) RSSI 확인
4. RPi `sensing`(:5005, 방식 B) 수신 확인
5. 통과 시 `v0.1.0-field` 태그 추가
