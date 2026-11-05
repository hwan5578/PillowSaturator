# Pillow Saturator (v0.2)

Pillow Audio의 세츄레이션 플러그인입니다. JUCE 8 기반이며 VST3, AU(macOS), Standalone으로 빌드됩니다.
Saturn 2, Decapitator, Clariphonic 분석에서 얻은 공통 패턴을 따르되, 곡선과 튜닝은 모두 새로 설계했습니다.
공통 패턴은 다음과 같습니다.

- 곡선 앞뒤에 EQ를 둔다.
- 레벨에 반응하는 요소를 둔다.
- 바이어스로 짝수 배음을 만든다.
- Auto Gain과 지연 보정 Mix를 둔다.

## 신호 흐름

```
In → Input → [M/S] → Low Cut → 스타일 Pre-EQ → Drive → 엔벨로프/바이어스
   → ↑오버샘플 → [+ 패러렐 세츄레이션 (Focus 대역)] → 곡선(ADAA) [→ Glow 2단]
   → ↓다운샘플 (+정확한 지연 패딩) → DC 제거
   → 스타일 Post-EQ → Air → Tone(틸트) → High Cut → Auto Gain → [M/S 복원]
   → Mix(지연 맞춘 드라이와 혼합) → Output → 바이패스/전환 페이드 → Out
```

- 패러렐 경로는 메인 곡선 **앞**에서 합쳐집니다. 그래서 패러렐이 만든 배음도 선택한 스타일을 한 번 더 거칩니다.

## 파라미터

| 이름 | 범위 | 기본값 | 설명 |
|---|---|---|---|
| Input | −24 … +24 dB | 0 dB | 입력 레벨 (Auto Gain이 켜져 있으면 세츄레이션 양만 바뀜) |
| Drive | 0 … +36 dB | +6 dB | 곡선에 들어가는 레벨 |
| Style | Tube / Tape / Transformer / Clean / Glow | Tube | 아래 표 참고 |
| Character | 0 … 100 % | 35 % | 0 %는 홀수 배음만, 올릴수록 짝수 배음(비대칭). Glow에서는 UI 이름이 **2ND STAGE**로 바뀌고 2단의 드라이브·비대칭·새그를 올림 |
| Tone | Dark 100 … Bright 100 | Flat | 700 Hz 기준 틸트 EQ (±4.5 dB 셸프 한 쌍) |
| Low Cut | 10 Hz … 1 kHz | 10 Hz | 세츄레이션 **앞** 12 dB/oct 하이패스 |
| High Cut | 1 kHz … 20 kHz / Off | Off | 세츄레이션 **뒤** 12 dB/oct 로우패스 |
| Mix | 0 … 100 % | 100 % | 지연이 정확히 맞춰진 드라이 신호와 혼합 |
| Output | −24 … +24 dB | 0 dB | 최종 출력 |
| Auto Gain | On / Off | On | −18 dBFS(0 VU) 기준으로 입력과 출력 레벨을 맞춤 |
| Quality | Normal 2x / High 4x / Ultra 8x | High | 오버샘플링 배수. 88.2 kHz 이상에서는 자동으로 절반, 176.4 kHz 이상에서는 1/4 (최소 2x) |
| Linear Phase | On / Off | Off | 선형 위상 오버샘플링 필터 |
| Stereo Mode | Stereo / Mid-Side | Stereo | M/S 모드에서는 Mid와 Side를 따로 세츄레이션 |
| Parallel | 0 … 100 % | 0 % | 패러렐 세츄레이션 양 (아래 설명) |
| Parallel Focus | Low / Flat / High | Flat | 패러렐 경로가 듣는 대역: Low = 350 Hz 이하, Flat = 전 대역, High = 2.2 kHz 이상 |
| Parallel Alt | On / Off | Off | 더 뜨겁고(+6 dB) 더 비대칭인 패러렐 보이싱 |
| Air | On / Off | Off | 세츄레이션 뒤 부드러운 고역 셸프 (1 kHz +0.0, 12 kHz +1.5, 16 kHz +2.6, 19 kHz +3.3 dB) |

## 스타일 설계

다섯 스타일은 같은 골격을 쓰고 레시피만 다릅니다. 레시피는 `Source/dsp/Styles.h` 한 곳에 모여 있습니다.
Pre/Post 셸프는 정확한 역필터 쌍이라서, 작은 신호에서는 평탄하고 어느 대역이 먼저 포화되는지만 바뀝니다.

| 스타일 | 곡선 | EQ 설계 | 동적 요소 |
|---|---|---|---|
| Tube | tanh (단단한 무릎) | 22 Hz HP(커플링 캡), 16 kHz LP, 180 Hz +0.6 dB | **새그**: 엔벨로프가 바이어스를 더 밀어 큰 소리에서 짝수 배음과 압축이 늘어남 |
| Tape | x/√(1+x²) (부드러운 무릎) | 3 kHz 셸프 ±7 dB 엠퍼시스(고역이 먼저 포화), 55 Hz 헤드 범프 +1.5 dB, 18 Hz HP | 레벨이 커질수록 고역 손실 (19 kHz → 최저 5 kHz) |
| Transformer | tanh | 160 Hz 셸프 ±9 dB 엠퍼시스(저역이 먼저 포화), 17 kHz +0.7 dB 공진, 12 Hz HP | 없음 (주파수 의존 포화 자체가 핵심) |
| Clean | 3차 소프트 클립 (±1.5에서 천장) | 없음 | 없음. 가장 투명함 |
| Glow | 2단 직렬: 1단 tanh(대칭, Drive) → 2단 소프트(비대칭, 2ND STAGE 0…+15 dB) | 110 Hz 셸프 ±3 dB 엠퍼시스(저역 무게감), 90 Hz +0.7 dB, 20 Hz HP | 2단에 **새그**. 단 사이 30 kHz 대역 제한(진공관 단의 대역폭처럼, 1단의 초고역 배음이 2단에서 에일리어싱으로 접히지 않게 함) |

### 패러렐 세츄레이션

- Focus로 고른 대역의 복사본을 별도의 진공관형 곡선에 세게(기본 +12 dB, Alt +18 dB와 큰 바이어스) 넣습니다.
- Amount는 그 대역을 깨끗한 소리에서 포화된 소리로 서서히 바꾸고, 그 대역을 살짝(100 %에서 +1.9 dB) 강조합니다.
  - 그래서 Focus는 큰 셸프 EQ처럼 동작하지 않습니다. **배음이 어느 대역에서 생기는지**를 바꿉니다.
  - 예: Low는 베이스의 배음과 무게감, High는 고역의 반짝임과 bite.
- 선형 성분은 지연·필터 없이 그대로 더하고, 비선형 잔차(배음과 압축)만 30 kHz로 대역 제한합니다. 그래서 Quality와 무관하게 응답이 평탄합니다. 작은 신호 기준으로 Flat은 60 Hz–19.5 kHz에서 ±0.00 dB입니다.
- Focus와 Alt를 바꾸면 패러렐 경로만 10 ms 동안 페이드됩니다. 메인 소리는 끊기지 않습니다.
- Auto Gain은 패러렐을 드라이브 증가로 환산해서 보정합니다.

## 측정 결과

저장소의 엔진 테스트(`tests/EngineTests.cpp`)로 이 환경에서 측정했습니다.

**배음** (1 kHz, −18 dBFS RMS, Character 35 %)

| 스타일 | Drive 0 dB | Drive +12 dB | Drive +24 dB |
|---|---|---|---|
| Tube | THD 1.1 % (H2 −40, H3 −52 dB: 2차 우세) | 6.0 % | 25 % |
| Tape | 0.74 % | 3.7 % | 15 % (가장 부드러움) |
| Transformer | 1.2 % | 5.4 % | 25 % |
| Clean | 0.33 % | 2.05 % | 26 % |
| Glow | 3.0 % (H2 −32, H3 −37 dB) | 13.9 % | 34 % (가장 진함) |

- Character 0 %에서는 모든 스타일의 짝수 배음이 −175 dB 이하입니다 (순수 홀수 배음).
- 주파수 의존성 (Drive +12 dB, H3 기준)
  - Transformer: 60 Hz −12.7 dB, 1 kHz −28 dB. 저역이 15 dB 더 포화됩니다.
  - Tape: 5 kHz −26 dB, 1 kHz −30 dB. 고역이 더 포화됩니다.
- Auto Gain: 모든 스타일에서 Drive 0~36 dB, Character 0/50/100 % 전 구간 동안 출력 레벨이 입력 대비 ±0.05 dB 이내입니다.
  - 패러렐을 켰을 때(Drive 6/18 dB, Input 0/+12 dB, Amount 50/100 %)는 최악 +0.44 dB(Flat)입니다. Low와 High는 ±0.05 dB 이내입니다.

**패러렐 세츄레이션** (Clean, Drive 0, Character 0, Amount 100 %, −18 dBFS)

| Focus | 60 Hz THD | 1 kHz THD | 5 kHz THD |
|---|---|---|---|
| 꺼짐 | 0.10 % | 0.10 % | 0.09 % |
| Low | **4.45 %** | 0.12 % | 0.09 % |
| Flat | 4.46 % | **4.44 %** | 4.22 % |
| High | 0.10 % | 0.25 % | **4.25 %** |

- Alt를 켜면 Flat 1 kHz THD가 4.4 % → 11.7 %로 오르고, H2가 −29 → −21 dB로 커집니다.

**에일리어싱** (7 kHz, −6 dBFS 피크, Drive +18 dB, 48 kHz에서 가청대역 최악값)

| 스타일 | 2x | 4x | 8x (최소 위상 / 선형 위상) |
|---|---|---|---|
| Tube | −63 dB | −113 dB | −122 / −126 dB |
| Tape | −57 dB | −84 dB | −109 / −119 dB |
| Transformer | −53 dB | −110 dB | −120 / −142 dB |
| Clean | −63 dB | −90 dB | −119 / −123 dB |
| Glow | −55 dB | −87 dB | −114 / −129 dB |
| Glow + 패러렐 Alt 100 % | −57 dB | −106 dB | −110 / −128 dB |

- 2단 직렬이나 패러렐처럼 곡선이 겹치면 배음이 더 많이 생깁니다. 그래서 Glow는 **High(4x) 이상**을 권장합니다.

**지연**

| 모드 | 2x | 4x | 8x | 보고되는 지연 |
|---|---|---|---|---|
| 최소 위상 | 5 | 6 | 6 | 6 샘플 (Quality와 무관하게 고정) |
| 선형 위상 | 66 | 71 | 73 | 73 샘플 ≈ 1.5 ms @48 kHz |

- Quality를 바꿔도 지연은 그대로이고, 지연은 Linear Phase를 켜고 끌 때만 바뀝니다. 그래서 Cubase가 지연을 다시 보정할 일이 거의 없습니다.
- 선형 위상 모드에서 드라이/웻 위상 오차는 5 kHz에서 약 0.2°입니다.
  - 필터, ADAA의 반 샘플, 분수 지연까지 계산해 정수 샘플로 맞췄기 때문입니다.
  - 1 kHz에서 약 1°의 차이는 10 Hz Low Cut과 DC 블로커 때문입니다.

**CPU** (스테레오 48 kHz, 이 클라우드 머신의 코어 1개 기준)

| Quality | 최소 위상 | 선형 위상 |
|---|---|---|
| Normal (2x) | 0.9–2.3 % | 1.0–1.9 % |
| High (4x) | 1.3–3.0 % | 1.4–2.8 % |
| Ultra (8x) | 1.7–4.1 % | 2.5–5.8 % |
| Glow + 패러렐 + Air (최대 부하) | 2.4 / 4.8 / 7.8 % (2x/4x/8x) | 4.0 / 4.4 / 7.3 % |

- 범위는 스타일별 차이입니다. 3회 측정의 최솟값이며, 클라우드 머신이라 잡음이 있습니다.

**안정성**
- 랜덤 오토메이션 400블록 × 샘플레이트 4종 × 모노/스테레오에서 NaN과 Inf가 0건이었습니다.
- 스타일, Quality, 위상, 스테레오, 패러렐 Focus/Alt, Air 전환과 바이패스 토글 28회 동안 클릭이 없었습니다 (샘플 간 최대 변화 0.005).

## VST3 검증 결과

실제 JUCE 8.0.17-security로 VST3를 빌드해서 검증했습니다. 빌드는 Linux에서 했고, 소스는 Windows 빌드와 같습니다.

- **Steinberg 공식 VST3 validator** (VST3 SDK 3.8.1): 표준 **47/47**, 확장 모드(`-e`) **537/537** 통과
- **호스트 로드 테스트**: DAW처럼 플러그인을 불러와 파라미터를 텍스트로 설정하고 오디오를 처리했습니다.
  - THD가 엔진 측정값과 소수점 둘째 자리까지 일치합니다 (예: Tube +24 dB = 25.18 %, Glow +12 dB = 13.94 %, 패러렐 Low 60 Hz = 4.45 %).
  - Auto Gain 출력이 −18.00 dBFS로 유지됩니다.
  - Air는 1 kHz +0.01 dB, 16 kHz +2.55 dB입니다.
  - 지연 6/73 샘플과 상태 저장·복원(새 파라미터 포함)도 확인했습니다.

## Windows VST3 받기 — 방법 1: GitHub Actions (내 PC에 도구 설치 불필요)

`.github/workflows/build.yml`이 들어 있습니다. GitHub의 Windows 서버(MSVC)가 VST3를 대신 빌드합니다.

1. GitHub에서 새 저장소를 만들고, 이 폴더의 내용을 그대로 올립니다.
   - 웹에서 "Add file → Upload files"로 끌어다 놓아도 됩니다.
   - `.github` 폴더가 꼭 포함되어야 합니다.
2. 저장소의 **Actions** 탭 → **Build** 실행이 끝날 때까지 기다립니다. 처음에는 약 10–20분 걸립니다.
3. 실행 결과 아래 **Artifacts**에서 `PillowSaturator-Windows-VST3`를 내려받아 압축을 풉니다.
   - main에 푸시한 빌드는 `builds` 브랜치에도 zip으로 올라갑니다.
4. `Pillow Saturator.vst3`를 `C:\Program Files\Common Files\VST3\`에 복사하고, Cubase에서 다시 스캔합니다.

## Windows VST3 받기 — 방법 2: 내 PC에서 빌드

1. **Visual Studio 2022**를 설치하고 "C++를 사용한 데스크톱 개발" 워크로드를 선택합니다. CMake가 포함됩니다. **Git**도 필요합니다.
2. 이 폴더에서 아래 명령을 실행합니다. 처음 구성할 때 JUCE `8.0.17-security`를 자동으로 내려받으므로 인터넷이 필요합니다.
   ```
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --target PillowSaturator_VST3
   ```
   - JUCE가 이미 있다면 `-DPILLOW_JUCE_PATH=C:/경로/JUCE`를 붙이면 됩니다.
   - Visual Studio에서 "폴더 열기"로 열어도 됩니다.
3. 결과물은 `build/PillowSaturator_artefacts/Release/VST3/Pillow Saturator.vst3`입니다.
   - 이것을 `C:\Program Files\Common Files\VST3\`에 복사합니다.
   - Cubase에서 Studio → VST Plug-in Manager로 다시 스캔합니다.
4. 빠르게 들어 보려면 Standalone을 빌드합니다: `--target PillowSaturator_Standalone`
5. 엔진 테스트를 돌리려면 `--target PillowEngineTests`로 빌드한 뒤 `ctest -C Release --test-dir build`를 실행합니다. JUCE 없이도 빌드됩니다. `-DPILLOW_BUILD_PLUGIN=OFF`로 구성하면 테스트만 빌드합니다.

## 폴더 구조

```
CMakeLists.txt
Source/
  dsp/                 ← JUCE 비의존 DSP 엔진 (헤더 전용 C++17)
    SaturatorEngine.h  ← 전체 신호 경로, Auto Gain 테이블, 바이패스·전환 페이드
    Styles.h           ← 스타일 레시피 (튜닝은 여기서)
    Shapers.h          ← 곡선 + 원시함수 (ADAA)
    Oversampler.h      ← 하프밴드 IIR/FIR 캐스케이드, 지연 정수화
    Filters.h, Smoothing.h, DspCommon.h
  Parameters.h/.cpp    ← 파라미터 ID·범위·표시 형식
  PluginProcessor.*    ← JUCE 래퍼 (APVTS, 지연 보고, 바이패스)
  PluginEditor.*       ← UI
  ui/                  ← LookAndFeel, 전달 곡선 디스플레이
tests/EngineTests.cpp  ← 측정·회귀 테스트
```

## 튜닝 방법

- `Styles.h`에서 스타일 레시피를 바꾸세요.
  - 바꿀 수 있는 값은 곡선, sensitivity, biasScale, sagAmount, hfLossAmount, pre/post 필터입니다.
  - Auto Gain 테이블은 시작할 때 레시피로부터 자동 계산되므로 따로 맞출 필요가 없습니다.
- 바꾼 뒤에는 `PillowEngineTests`를 다시 돌려 배음, Auto Gain, 에일리어싱이 원하는 범위인지 확인하세요.
- 새 스타일을 추가하려면 `kNumStyles`를 늘리고, 레시피 하나를 넣고, `Parameters.h`의 `styleNames()`에 이름을 추가하면 됩니다.
- Glow의 2단은 `twoStage`, `curve2`, `stage2MaxDriveDb`, `stage2BiasScale`로 조절합니다.
- 패러렐은 `Styles.h` 위쪽에 모여 있습니다.
  - 보이싱(드라이브, 바이어스)은 `getParallelVoicing`에 있습니다.
  - Focus 주파수는 `kParallelLowHz`와 `kParallelHighHz`입니다.
  - 대역 강조량은 `kParallelLinear`입니다.
  - 단 사이 대역 제한은 `kInterstageHz`입니다.

## 알아둘 점

- **플러그인 코드**: `PLUGIN_MANUFACTURER_CODE`(Pilw)는 다른 Pillow 플러그인과 **같은 값**이어야 합니다. `PLUGIN_CODE`(PSat)는 출시 후 절대 바꾸지 마세요. 바꾸면 기존 세션에서 플러그인을 찾지 못합니다.
- **파라미터 ID**: `Parameters.h`의 ID도 출시 후에는 바꾸면 안 됩니다. 저장된 세션과 프리셋이 이 ID를 참조합니다.
- **스위치형 파라미터**: Style, Quality, Linear Phase, Stereo Mode는 바꿀 때 약 15–20 ms 동안 소리를 줄였다가 다시 올립니다. 전환 중 클릭을 막기 위한 동작입니다. Parallel Focus와 Alt는 패러렐 경로만 10 ms 페이드합니다.
- **v0.1 세션 호환**: 저장된 세션과 프리셋은 그대로 열립니다. Style 목록이 4개에서 5개로 늘어서, v0.1에서 Style을 **오토메이션으로 기록**했다면 그 값은 다시 확인해야 합니다.
- **바이패스**: 호스트 바이패스는 12 ms 크로스페이드로 처리됩니다. 지연 보정이 유지되므로 타이밍이 어긋나지 않습니다.
- **Auto Gain 기준**: −18 dBFS = 0 VU에서 정확히 맞습니다. 이보다 큰 신호는 자연스럽게 압축되고, 작은 신호는 약간 커집니다.
- **라이선스**: JUCE 8을 상업적으로 배포하려면 JUCE 라이선스 조건(무료 티어, 상용, AGPLv3)을 확인하세요.
- **JUCE 버전**: JUCE 9가 이미 나와 있습니다. 이 프로젝트는 JUCE 8.0.17-security 기준이며, 나중에 `PILLOW_JUCE_TAG`로 버전을 올리면 됩니다.
- **검증 범위**: 실제 JUCE 8.0.17로 VST3를 빌드했고 Steinberg validator와 호스트 테스트를 통과했습니다. 다만 그 빌드는 Linux 빌드였습니다. Windows 빌드는 GitHub Actions(MSVC)에서 엔진 테스트까지 통과하지만, Cubase 안에서의 화면·동작은 아직 직접 확인하지 못했습니다.

## 다음 단계 아이디어

- **Dual Mono**: 좌우 독립 파라미터 (Clariphonic 방식)
- **Drive Pan**: L/R 또는 M/S별 드라이브 차이 (Saturn 방식)
- **Tape 히스테리시스**: Jiles–Atherton 모델 기반
- **멀티밴드 모드**: 크로스오버 + 밴드별 스타일
- **편의 기능**: 프리셋 매니저, 크기 조절 UI, 입출력 미터 보정
