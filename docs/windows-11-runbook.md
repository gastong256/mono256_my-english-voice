# Windows 11 Runbook

Guia operativa para clonar, preparar, validar y correr `my-english-voice` en Windows 11.

Este documento esta pensado para:

- un clone nuevo del repo en Windows
- desarrollo y pruebas reales con microfono, TTS y VB-Cable
- validacion del camino CPU primero y CUDA despues

## 1. Objetivo De Este Runbook

Al terminar esta guia deberias poder:

1. preparar un host Windows 11 limpio
2. clonar el repo en una ubicacion soportada
3. bootstrapear dependencias y modelos
4. compilar el binario Windows
5. validar audio, ASR, TTS y CUDA con `--self-test`
6. correr el modo `interactive_preview`
7. correr el modo `interactive_balanced`
8. ejecutar benchmarks locales de latencia
9. probar el loop completo con VB-Cable en Meet / Zoom / Teams

## 2. Ubicacion Soportada Del Repo

Usa una ruta de Windows visible para ambos entornos:

```text
C:\dev\my-english-voice
```

Si ademas vas a usar WSL2, la ruta equivalente debe ser:

```text
/mnt/c/dev/my-english-voice
```

No uses:

- `C:\Users\<usuario>\Downloads\...`
- rutas con sincronizacion agresiva tipo OneDrive si puedes evitarlas
- clones dentro de `/home/...` si luego vas a invocar wrappers WSL2 -> Windows

## 3. Prerequisitos De Windows 11

### 3.1 Requisitos Obligatorios

Instala esto antes de correr scripts del repo:

1. Windows 11 actualizado
2. Visual Studio 2022 o Build Tools 2022
3. workload de C++ con CMake tools
4. Git for Windows
5. PowerShell con policy local habilitada
6. VB-Cable

### 3.2 Visual Studio 2022

Instala Visual Studio 2022 o Build Tools con soporte C++.

Debes tener al menos:

- MSVC v143
- C++ CMake tools for Windows
- Ninja
- Windows 10/11 SDK

Verificacion recomendada en PowerShell:

```powershell
cmake --version
ninja --version
git --version
```

### 3.3 PowerShell Execution Policy

Habilita scripts locales del proyecto:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

### 3.4 Driver GPU

Si vas a probar CUDA:

1. instala driver NVIDIA actualizado
2. reinicia Windows
3. verifica desde PowerShell:

```powershell
nvidia-smi
```

Si `nvidia-smi` no funciona, no intentes validar el camino CUDA todavia.

### 3.5 VB-Cable

Instala VB-Cable desde VB-Audio.

Despues de instalarlo:

1. reinicia si el instalador lo pide
2. abre configuracion de sonido de Windows
3. confirma que existen dispositivos parecidos a:
   - `CABLE Input`
   - `CABLE Output`

## 4. Configuracion Recomendada De Audio En Windows

Antes de correr la app, revisa:

1. tu microfono real funciona en Windows
2. VB-Cable aparece como dispositivo de entrada/salida
3. en Propiedades de sonido desactiva mejoras agresivas si notas glitches
4. si un dispositivo falla al abrirse, prueba con sample rates estandar del sistema antes de tocar el codigo

Consejo practico:

- la app busca nombres por substring
- si `--list-devices both` muestra un nombre un poco distinto, usa ese substring exacto en `audio.output_device`

## 5. Clonado Del Repo En Windows

En PowerShell:

```powershell
mkdir C:\dev -Force
cd C:\dev
git clone <URL_DEL_REPO> my-english-voice
cd .\my-english-voice
```

Si ya tienes el repo en Linux/WSL2, tambien puedes abrir exactamente esa misma ruta desde Windows mientras viva bajo `C:\dev\...`.

## 6. Bootstrap Del Entorno

Desde la raiz del repo en PowerShell:

```powershell
.\scripts\windows\setup-dev.ps1
```

Este script hace:

- prepara `vcpkg` en `.local\vcpkg`
- instala paquetes nativos pinneados
- descarga ONNX Runtime pinneado (actualmente v1.22.0 con soporte CUDA 12.x)
- descarga modelos pinneados en `models\`:
  - `ggml-small.bin` (Whisper small FP16, para GPU)
  - `ggml-small-q5_1.bin` (Whisper small Q5_1, para CPU baseline)
  - `ggml-tiny.bin` (Whisper tiny FP16)
  - `ggml-tiny-q5_1.bin` (Whisper tiny Q5_1, para modo MINIMAL en CPU)
  - `en_US-lessac-medium.onnx` + `.onnx.json` (Piper TTS)
- escribe `.local\windows-dev-env.ps1`

Resultado esperado:

- no hay errores
- ves un resumen final con `VCPKG_ROOT`, `ONNXRUNTIME_ROOT` y siguientes pasos

Nota importante sobre eSpeak en Windows:

- el repo intenta provisionarlo si la registry actual de `vcpkg` lo soporta
- si el port no existe en tu entorno, `setup-dev.ps1` sigue adelante y te deja el resto del host listo
- si necesitas `MEV_ENABLE_ESPEAK=ON` y vcpkg falla, instala eSpeak-ng manualmente:
  1. descarga el installer desde `https://github.com/espeak-ng/espeak-ng/releases`
  2. instala en la ruta por defecto (`C:\Program Files\eSpeak NG`)
  3. antes de compilar: `$env:ESPEAK_ROOT = "C:\Program Files\eSpeak NG"`

Carga luego el helper local:

```powershell
. .\.local\windows-dev-env.ps1
```

## 7. Build Recomendado

### 7.1 Build Completo

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-full
```

Usa este preset para pruebas reales con:

- PortAudio
- Whisper
- Piper
- eSpeak
- libsamplerate

### 7.2 Build Smoke

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-smoke
```

Usa este preset solo si quieres aislar problemas y validar el camino mas liviano.

### 7.3 Build CI-Safe

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-ci
```

Usa este preset si quieres reproducir el mismo contrato que usa GitHub Actions:

- Whisper habilitado
- benchmarks habilitados
- sin dependencia de eSpeak en Windows

### 7.4 Build GPU Debug (para depuracion de problemas CUDA)

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-gpu-debug
```

Usa este preset para depurar el camino GPU con simbolos de debug activos:

- todos los backends habilitados (identico a `windows-msvc-full` en backends)
- modo Debug — binario mas lento pero con informacion de debug completa
- util cuando `--self-test` muestra fallback a CPU y necesitas rastrear el origen

## 8. Validaciones Iniciales Obligatorias

Haz estas pruebas en este orden.

### 8.1 Test Suite Windows

```powershell
.\scripts\windows\test.ps1 -Preset windows-msvc-full
```

Resultado esperado:

- `ctest` completo sin fallos

### 8.2 Enumerar Dispositivos

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml -AppArgs @('--list-devices','both')
```

Confirma:

- aparece tu microfono real
- aparece un dispositivo con nombre parecido a `CABLE Input`

Si no aparece `CABLE Input`, no sigas con pruebas reales hasta resolver eso.

### 8.3 Self-Test CPU Baseline

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.preview.toml
```

Resultado esperado:

- config valida
- modelos presentes
- backend de audio seleccionado correctamente
- ASR y TTS inicializan

### 8.4 Self-Test Balanced

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml
```

Resultado esperado:

- Piper inicializa o, si algo falla, el motivo queda claro
- eSpeak queda disponible como fallback

### 8.5 Self-Test CUDA

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.cuda.toml
```

Busca en el output:

- deteccion de GPU
- visibilidad del runtime CUDA
- requested vs effective placement para ASR y TTS

Interpretacion:

- si Whisper o Piper quedan en CPU con razon clara, el diagnostico esta funcionando
- si el objetivo es validar CUDA, no lo des por bueno hasta ver placement efectivo en GPU

## 9. Primera Corrida Segura

Antes de ir al camino full, valida el modo de menor riesgo.

### 9.1 Preview Real-Audio

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.preview.toml -AppArgs @('--runtime.run_duration_seconds','20')
```

Este modo usa:

- ASR en baseline CPU (Whisper small Q5_1)
- eSpeak como engine principal
- politica de menor latencia

Que debes observar:

- no crashea
- abre microfono real
- emite logs `[METRICS]`
- no hay underruns o drops descontrolados

### 9.2 Balanced Real-Audio

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml -AppArgs @('--runtime.run_duration_seconds','20')
```

Este modo intenta el camino principal:

- Whisper translate (Whisper small Q5_1)
- Piper como TTS principal
- fallback a eSpeak si hace falta

Que debes observar:

- el run arranca y completa
- hay `speech_chunks` en logs
- si Piper excede presupuesto, ves preview/fallback pero el sistema sigue respondiendo

### 9.3 CUDA Real-Audio

Haz esto solo despues de validar preview y balanced sin CUDA:

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.cuda.toml -AppArgs @('--runtime.run_duration_seconds','20')
```

### 9.4 Produccion CPU (runtime indefinido)

Una vez validado el camino completo, usa el config de produccion para sesiones reales:

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.production.toml
```

Este config tiene `run_duration_seconds = 0` que significa **correr hasta CTRL+C**.
Usa `config/pipeline.windows.cuda.production.toml` para la variante GPU.

### 9.5 VAD Real-Audio (opcional, para comparacion de latencia)

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.webrtcvad.toml
```

Con VAD habilitado, el ASR solo recibe audio cuando hay voz detectada, reduciendo
inferencias innecesarias en silencio. Compara `asr_q` en `[HEALTH]` logs vs. el config sin VAD.

## 10. Pruebas Manuales Reales De Conversacion

Una vez que los runs cortos funcionan:

1. deja `config/pipeline.windows.preview.toml` para priorizar baja latencia
2. habla frases cortas de dominio tecnico
3. verifica que el ingles sea entendible, aunque no sea nativo

Frases recomendadas:

- `necesitamos revisar la base de datos antes del release`
- `el deploy fallo por una variable de entorno faltante`
- `puedes reiniciar el servicio y revisar los logs`
- `la latencia subio despues del cambio en la API`
- `hay que validar la migracion de PostgreSQL en produccion`

Que evaluar:

- meaning preserved
- terminos tecnicos conservados
- claridad para dialogo corto
- tiempo hasta primer audio

## 11. Benchmark De Latencia

### 11.1 Benchmark Local

```powershell
.\scripts\windows\benchmark-latency.ps1 -BuildFirst
```

Esto genera artefactos en:

```text
artifacts\benchmarks\<timestamp>\
```

### 11.2 Benchmark Con Audio Simulado

Si quieres aislar problemas de dispositivos:

```powershell
.\scripts\windows\benchmark-latency.ps1 -BuildFirst -AppArgs @('--runtime.use_simulated_audio','true')
```

### 11.3 Validar Guardrails

```powershell
python .\scripts\check_realtime_regressions.py `
  --latency-summary artifacts\benchmarks\<timestamp>\summary.json `
  --latency-thresholds benchmarks\regression_thresholds.json
```

Objetivos actuales:

- `interactive_preview`: `TTFA_audio_p50 <= 450 ms`
- `interactive_balanced`: `TTFA_audio_p50 <= 650 ms`

## 12. Validacion Conversacional

Cuando ya tengas una corrida real con predicciones exportadas:

1. genera o guarda `prediction_en` por sample
2. usa `eval/domain_realtime_set.jsonl`
3. emite CSV de revision
4. completa revision manual
5. compara contra thresholds de calidad

Comandos:

```powershell
python .\eval\score_domain_eval.py `
  --dataset .\eval\domain_realtime_set.jsonl `
  --predictions .\eval\your_predictions.jsonl `
  --emit-review-csv .\eval\manual_review.csv
```

Luego de revisar el CSV:

```powershell
python .\eval\score_domain_eval.py `
  --dataset .\eval\domain_realtime_set.jsonl `
  --predictions .\eval\your_predictions.jsonl `
  --manual-labels .\eval\manual_review.csv `
  --summary-out .\eval\manual_review_summary.json
```

Y compara contra guardrails:

```powershell
python .\scripts\check_realtime_regressions.py `
  --quality-summary .\eval\manual_review_summary.json `
  --quality-thresholds .\eval\conversational_thresholds.json
```

## 13. Prueba End-To-End Con VB-Cable

### 13.1 Configuracion De La App

Usa como baseline:

- `config/pipeline.windows.preview.toml` para latencia minima
- `config/pipeline.windows.toml` para modo principal balanceado

Verifica que en el TOML:

```toml
[audio]
output_device = "CABLE Input"
```

### 13.2 Configuracion De La Videollamada

En Meet / Zoom / Teams:

1. deja tu microfono real seleccionado dentro de la app `my-english-voice`
2. en la app de videollamada selecciona `CABLE Output` como microfono
3. habla por tu microfono fisico
4. confirma que la videollamada recibe la voz sintetizada y no tu voz cruda

### 13.3 Prueba Recomendada

Haz primero una llamada de prueba o grabacion local.

Valida:

- se oye audio sintetizado
- la latencia es tolerable
- no hay cortes largos
- el interlocutor entiende frases tecnicas cortas

## 14. Orden Recomendado De Validacion

No saltees este orden:

1. `setup-dev.ps1`
2. `build.ps1 -Preset windows-msvc-full`
3. `test.ps1`
4. `--list-devices both`
5. `self-test` preview
6. `self-test` balanced
7. `self-test` CUDA
8. run preview real-audio (20s)
9. run balanced real-audio (20s)
10. run CUDA real-audio (20s)
11. run production CPU (CTRL+C para parar)
12. run production CUDA (CTRL+C para parar)
13. run VAD (opcional, comparacion de latencia)
14. benchmark local
15. validacion con VB-Cable
16. validacion conversacional manual

## 15. Troubleshooting Rapido

### 15.1 `RepoRoot must be a Windows path`

Estas corriendo un script Windows desde una ruta no soportada.

Solucion:

- mueve el repo a `C:\dev\my-english-voice`

### 15.2 `ASR model path not found`

Faltan modelos en `models\`.

Solucion:

- rerun `.\scripts\windows\setup-dev.ps1`

### 15.3 `Executable not found`

No compilaste el preset pedido.

Solucion:

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-full
```

### 15.4 `CABLE Input` no aparece

VB-Cable no esta instalado o Windows no lo expone todavia.

Solucion:

- reinstalar VB-Cable
- reiniciar Windows
- volver a correr `--list-devices both`

### 15.5 Self-test cae a CPU

El diagnostico de CUDA o de ONNX Runtime esta detectando una degradacion.

Revision:

- `nvidia-smi`
- driver NVIDIA
- output de `--self-test --config config/pipeline.windows.cuda.toml`
- visibilidad de `onnxruntime_providers_cuda.dll`

### 15.6 ONNX Runtime CUDA no carga (`onnxruntime_providers_cuda.dll` no visible)

El self-test reporta `requested_device=gpu effective=cpu` para Piper.

Posibles causas y revision:

1. **Version mismatch**: ONNX Runtime v1.22.0 requiere CUDA 12.x. Verifica con `nvidia-smi` que el
   driver soporta CUDA 12.x o superior
2. **DLL no encontrado**: verifica que `ONNXRUNTIME_ROOT` apunta al directorio correcto:
   ```powershell
   ls $env:ONNXRUNTIME_ROOT\lib\onnxruntime_providers_cuda.dll
   ```
3. **Driver desactualizado**: actualiza el driver NVIDIA al mas reciente y reinicia
4. **Visual C++ Redistributable**: asegurate de tener instalado el VC++ 2022 Redistributable

Si el problema persiste, el pipeline cae a CPU automaticamente gracias a `gpu_failure_action = "fallback_cpu"` en el config.

### 15.7 Audio entrecortado

Primero aísla si el problema es de dispositivo o de pipeline.

Prueba en este orden:

1. corre benchmark o run con `--runtime.use_simulated_audio true`
2. prueba `config/pipeline.windows.preview.toml`
3. revisa logs de `[METRICS]`
4. confirma que `queue_drops` y `output_underruns` no se disparan

## 16. Criterio Practico De Exito

Puedes considerar el host Windows "listo" si se cumple todo esto:

1. `setup-dev.ps1` termina sin errores
2. `windows-msvc-full` compila
3. `test.ps1` pasa
4. `--list-devices both` muestra microfono real y VB-Cable
5. `self-test` preview y balanced pasan
6. `self-test` CUDA al menos diagnostica correctamente el placement
7. `run.ps1` con preview y balanced completa una sesion corta sin crash
8. `benchmark-latency.ps1` produce `summary.json`
9. la videollamada recibe audio por `CABLE Output`

## 17. Estado Esperado Hoy

Hoy el repo ya deberia permitir:

- bootstrap y build en Windows 11
- validacion previa con `self-test`
- run real con configs Windows
- benchmark local de latencia
- pruebas manuales con VB-Cable

Lo que todavia debes confirmar en tu maquina:

- numeros reales de latencia
- calidad conversacional `B1/B2`
- camino positivo CUDA para Whisper y Piper
- estabilidad end-to-end en la app de videollamadas que uses
