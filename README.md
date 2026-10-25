# G14 2020 · PD Balance + 충전 표시

ASUS ROG Zephyrus G14 **GA401IU / BIOS 222**에서 쓰는 Windows 도구입니다.

- **PD Balance:** USB-C PD 사용 중 배터리 전류를 0mA 부근으로 유지합니다.
- **충전 표시:** 연결할 때 화면 아래에 3초간 표시합니다. PD는 초록색, 벽돌 충전기는 무지개이며 배터리 잔량만큼 펼쳐집니다.

## 설치

1. [Releases](https://github.com/Tl-I/ga401iu-pd-balance/releases)에서 실행 파일 ZIP을 받아 압축을 풉니다.
2. **PowerShell 7을 관리자 권한**으로 열고 해당 폴더에서 실행합니다.

```powershell
.\Install-PdAutostart.ps1
Start-ScheduledTask -TaskName 'GA401 PD Battery Balance'
Start-ScheduledTask -TaskName 'GA401 Charging Indicator'
```

이후 로그인할 때 두 기능이 자동 실행됩니다. 기존 사본이 동작 중이면 먼저 그 사본에서 정상 종료한 뒤 설치하세요.

## 중지

```powershell
.\Stop-PdSystemPower.ps1
.\Stop-ChargingIndicator.ps1
```

PD Balance는 정상 종료 시 EC 충전 요청으로 복원합니다. 실행 중인 `.pending` 파일은 지우지 마세요.

## 참고

**GA401IU / BIOS 222 한 대에서 검증했습니다.** 2022년형 GA402 등 다른 모델은 지원 대상으로 검증하지 않았습니다.
배터리를 물리적으로 분리하는 기능이 아니며, 진입·부하 변화 중에는 충방전이 발생할 수 있습니다.
PowerShell 7과 관리자 권한이 필요하고, 드라이버가 차단되면 보안 설정을 바꾸지 말고 중단하세요.

소스 빌드·모의 시험은 `Build.ps1`, `Test.ps1`로 실행합니다. [동작과 복구 세부 사항](docs/DETAILS.md).
프로젝트는 MIT이며 서명된 InpOutx64 드라이버의 원래 고지문을 함께 제공합니다.
