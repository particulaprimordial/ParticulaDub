# Particula Dub — VST3 para dub techno

Sintetizador de acordes con eco dub: 2 sierras → filtro resonante con envolvente → dub delay sincronizado → reverb.
Tocas una nota y el parámetro **Chord** genera el acorde (m7, m9, m11, sus m7).

## Descargar e instalar (sin compilar)
1. En este repo: pestaña **Actions** → último run de **Build VST3** → abajo, **Artifacts** → descarga el de tu sistema.
2. Windows: copia la carpeta `Particula Dub.vst3` a `C:\Program Files\Common Files\VST3`.
3. Mac: copia `Particula Dub.vst3` a `~/Library/Audio/Plug-Ins/VST3/` y en Terminal ejecuta:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Particula Dub.vst3"`
4. Ableton Live 12: Preferences → Plug-ins → activa **Use VST3 System Folders** → **Rescan**.
5. Arrastra **Particula Dub** (Plug-ins → Instruments) a una pista MIDI y toca.

## Contenido
| Ruta | Qué es |
|------|--------|
| `Source/` · `CMakeLists.txt` · `.github/` | Plugin VST3 (JUCE/C++) y compilación automática |
| `web/index.html` | Demo con interfaz visual (Web Audio) |
| `docs/TECHNICAL_REPORT.md` | Reporte técnico IMRaD |
| `validation/validate_dsp.py` | Modelo de referencia (NumPy) → `results.json` |

Sonido dub techno rápido: Chord m7 · Cutoff ~700 · Filter Env ~2.5 · Decay 0.6 · Sustain 0 · Delay 1/8. · Feedback 0.65 · Tone ~2200 · Mix 0.45 · Reverb 0.8 / 0.3
