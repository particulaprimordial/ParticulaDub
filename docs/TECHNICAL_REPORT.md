# Particula Dub: diseño y validación de un sintetizador de acordes con eco dub para dub techno

**Autor:** Diego José Palencia Robles · Partícula Primordial
**Versión del documento:** 0.1 · noviembre de 2026
**Código:** `Source/PluginProcessor.*` (plugin JUCE/VST3) · `web/index.html` (demo Web Audio) · `validation/validate_dsp.py` (modelo de referencia)

---

## Resumen

Particula Dub es un instrumento VST3 monofónico por nota (16 voces) orientado al “chord stab” característico del dub techno: un acorde corto, filtrado y repetido por un eco largo y oscuro. Este reporte describe su arquitectura (osciladores PolyBLEP, filtro de variable de estado TPT, generador de acordes MIDI, delay con realimentación saturada y reverb), deriva las ecuaciones que gobiernan cada etapa y las contrasta con mediciones sobre un modelo de referencia en Python. Resultados principales: (i) PolyBLEP reduce la energía inarmónica de una sierra en 16 dB respecto a la sierra ingenua en los tres tonos probados; (ii) la frecuencia de batido del detune coincide con la fórmula Δf = f₀(2^(c/1200) − 1) con error menor a 0,001 Hz; (iii) el lazo de eco decae exactamente según fb por repetición (0,65 medido = 0,65 predicho) y es estable incluso con fb = 0,95 y señal a escala completa; (iv) la ganancia del filtro en la frecuencia de corte es igual a la resonancia configurada (error < 0,01 dB). La validación se hizo sobre un modelo de referencia en Python, no sobre el binario C++; la comprobación en Ableton Live 12 queda pendiente (sección 5).

**Palabras clave:** dub techno, síntesis sustractiva, PolyBLEP, filtro TPT, delay con realimentación, VST3, JUCE.

---

## 1. Introducción

### 1.1 Contexto

El dub techno toma de la música dub jamaiquina la idea del estudio como instrumento: el eco y la reverberación dejan de ser un acabado y pasan a ser el material musical (Veal, 2007). En la práctica de producción, un acorde menor séptima corto se envía a un delay con realimentación filtrada, de modo que cada repetición es más oscura y quieta que la anterior, y se suma a un reverb largo. El resultado es una textura que se mueve lentamente sin necesitar muchas notas.

### 1.2 Problema y objetivo

Reproducir ese sonido en un DAW suele requerir encadenar varios dispositivos y ajustar sus parámetros por separado. El objetivo es un instrumento único, con pocos controles y valores por defecto musicalmente útiles, cuyo comportamiento esté **justificado matemáticamente y verificado numéricamente**.

### 1.3 Hipótesis de validación

| ID | Hipótesis |
|----|-----------|
| H1 | La corrección PolyBLEP reduce la energía inarmónica (aliasing) de una sierra frente a la sierra ingenua. |
| H2 | La frecuencia de batido entre las dos sierras sigue Δf = f₀(2^(c/1200) − 1). |
| H3 | El lazo de delay es estable y su decaimiento por repetición queda predicho por fb y por la respuesta del filtro de realimentación. |
| H4 | La resonancia del filtro de variable de estado fija la ganancia en la frecuencia de corte (|H(f_c)| = Q). |
| H5 | (Exploratoria) Los voicings de acorde ofrecidos difieren en rugosidad sensorial calculable. |

---

## 2. Métodos

### 2.1 Arquitectura

```
MIDI ─► Chord (1 nota → acorde) ─► 16 voces ─► Delay dub (L/R) ─► Reverb ─► Output
                                    │
                    2 × sierra PolyBLEP (detune) → LP TPT (env) → ADSR
```

Implementación: C++17 con JUCE (`juce::Synthesiser`, `juce::dsp::StateVariableTPTFilter`, `juce::dsp::DelayLine`, `juce::Reverb`), parámetros con `AudioProcessorValueTreeState`, formato VST3 (Steinberg) y entrada MIDI según MIDI 1.0 (MMA, 1996).

### 2.2 Oscilador

Cada voz suma dos sierras, la segunda con desafinación de *c* cents:

x[n] = ½ (s₁[n] + s₂[n]),  s_i[n] = (2φ_i[n] − 1) − polyBLEP(φ_i[n], Δ_i),  Δ_i = f_i / f_s

con φ ∈ [0,1) la fase y

polyBLEP(t, Δ) = t/Δ + t/Δ − (t/Δ)² − 1 si t < Δ;  ((t−1)/Δ)² + 2(t−1)/Δ + 1 si t > 1 − Δ;  0 en otro caso.

La sierra ingenua contiene armónicos por encima de Nyquist que se pliegan como componentes inarmónicas; PolyBLEP resta un residuo polinómico de dos puntos alrededor de cada discontinuidad (Välimäki y Huovilainen, 2007; Välimäki et al., 2012).

**Batido por desafinación.** Para dos frecuencias f₀ y f₀·2^(c/1200), la envolvente de amplitud de la fundamental oscila a

Δf = f₀ (2^(c/1200) − 1).

Con c = 12 cents (valor por defecto): Δf = 0,765 Hz en 110 Hz, 1,021 Hz en D3 (146,83 Hz) y 1,530 Hz en 220 Hz. Son modulaciones de amplitud lentas; la fuerza de fluctuación percibida tiene su máximo cerca de 4 Hz y la rugosidad se concentra en separaciones mucho mayores, del orden de decenas de Hz (Zwicker y Fastl, 1999), por lo que el detune por defecto se percibe como movimiento y no como aspereza.

### 2.3 Filtro y envolventes

Filtro pasa-bajos de variable de estado en topología TPT (Zavalishin, 2012), con

g = tan(π f_c / f_s),  k = 1/Q,  a₁ = 1 / (1 + g(g + k)),  a₂ = g a₁,  a₃ = g a₂.

Por construcción (prewarping), la respuesta analógica de segundo orden se mapea exactamente en f_c, donde |H| = Q.

La frecuencia de corte instantánea es

f_c(t) = clamp( f_base · 2^(A · e_f(t)), 20 Hz, 18 kHz ),

donde A (`envamt`) está en octavas y e_f(t) es una envolvente ADSR con ataque 3 ms, sustain 0 y decay `fdec`. Como los segmentos de `juce::ADSR` son lineales, e_f decrece linealmente y **f_c barre una trayectoria lineal en octavas** (exponencial en Hz), a una tasa de A/fdec octavas por segundo. Con los valores por defecto (700 Hz, A = 2,5, fdec = 0,35 s): pico de 3 960 Hz y barrido de 7,14 oct/s.

La envolvente de amplitud es un ADSR con parámetros independientes (`atk`, `dec`, `sus`, `rel`).

### 2.4 Generador de acordes

Antes del sintetizador, cada nota MIDI se expande a un conjunto de intervalos en semitonos (afinación temperada, f = 440·2^((n−69)/12) Hz):

| Modo | Intervalos | En D3 (Hz) |
|------|-----------|------------|
| Off | 0 | 146,83 |
| m7 | 0, 3, 7, 10 | 146,83 · 174,61 · 220,00 · 261,63 |
| m9 | 0, 3, 10, 14 | 146,83 · 174,61 · 261,63 · 329,63 |
| m11 | 0, 3, 7, 10, 17 | 146,83 · 174,61 · 220,00 · 261,63 · 392,00 |
| sus m7 | 0, 5, 7, 10 | 146,83 · 196,00 · 220,00 · 261,63 |

Como medida exploratoria (H5) se calculó la rugosidad de interacción entre notas con el modelo de disonancia sensorial de Plomp y Levelt (1965) en la parametrización de Sethares (2005), con 8 armónicos por nota de amplitud 1/h.

### 2.5 Delay dub

Por canal c ∈ {L, R}, con D_c muestras de retardo, filtro de un polo de coeficiente a = 1 − e^(−2π f_t / f_s) y realimentación fb:

w[n] = buf[n − D_c]  (salida del delay)
s[n] = s[n−1] + a (w[n] − s[n−1])  (filtro pasa-bajos de un polo, f_t = `dtone`)
buf[n] = tanh( x[n] + fb · s[n] )
y[n] = x[n] + mix · w[n]

El tiempo es sincronizado al tempo del anfitrión: D_L = m·(60/BPM)·f_s con m ∈ {¼, ½, ¾, 1, 1½} pulsos, y D_R = 0,75·D_L, lo que desfasa los canales y genera un patrón estéreo. El tiempo se suaviza (150 ms) para evitar saltos; los cambios de tiempo producen un efecto de cinta (variación de altura), deseado en este género.

**Estabilidad.** Con |tanh′| ≤ 1 y |H_lp(e^{jω})| ≤ 1 (ganancia 1 en DC, decreciente), la ganancia de lazo está acotada por fb < 1 (máximo 0,95), por lo que el sistema es estable en el sentido BIBO y la salida del lazo queda acotada por tanh(·) ∈ (−1, 1).

**Decaimiento.** La primera repetición es la señal seca retardada, sin filtrar; la repetición k+1 es la k-ésima multiplicada por fb y filtrada una vez más. En régimen lineal:

- el *área* de cada eco decae exactamente por fb (la ganancia en DC del filtro es 1);
- en una frecuencia f, cada repetición se atenúa por fb·|H_lp(f)|, con |H_lp|² = a² / (1 − 2(1−a)cos ω + (1−a)²);
- las repeticiones hasta −60 dB son 1 + ln(10⁻³)/ln(fb).

La oscuridad progresiva del eco (rasgo central del género) es, por tanto, consecuencia directa de aplicar el filtro dentro del lazo: tras k pasadas, la frecuencia de corte se atenúa aproximadamente 3k dB adicionales.

### 2.6 Reverb

`juce::Reverb`, basado en el esquema Schroeder–Moorer (combs en paralelo y allpass en serie; Schroeder, 1962) con las afinaciones de Freeverb. Parámetros expuestos: tamaño y mezcla (damping 0,6 fijo). Se aplica después del delay, de modo que los ecos también reverberan.

### 2.7 Protocolo de validación

El script `validation/validate_dsp.py` (Python 3.12.3, NumPy 2.5.4) reimplementa las etapas anteriores a f_s = 44 100 Hz y mide:

| Prueba | Hipótesis | Procedimiento |
|--------|-----------|---------------|
| Aliasing | H1 | Sierra de 2¹⁶ muestras a 220, 880 y 1 760 Hz; FFT con ventana Kaiser (β = 14); razón entre la energía fuera de ±6 bins de cada armónico y la de dentro, en dB. |
| Batido | H2 | Suma de dos sierras (c = 12 cents), 2²⁰ muestras; separación de los dos picos espectrales por interpolación parabólica en log. |
| Delay | H3 | Impulso de 0,01 (régimen lineal), T = 0,375 s (1/8. a 120 BPM), fb = 0,65, f_t = 2 200 Hz, 10 s; área y espectro a f_t de cada eco. Prueba de estabilidad: fb = 0,95, impulso de escala completa, 30 s. |
| Filtro | H4 | Respuesta al impulso del SVF TPT a 700 Hz; |H(f_c)| con la DTFT en f_c para Q = 0,707 · 1,2 · 3 · 6. |
| Acordes | H5 | Rugosidad de interacción = disonancia(acorde) − Σ disonancia(nota sola), root D3 (MIDI 50). |

Los resultados completos se guardan en `validation/results.json`.

---

## 3. Resultados

### 3.1 Aliasing (H1)

Energía inarmónica relativa a la armónica (menor = mejor):

| f₀ | Sierra ingenua | PolyBLEP | Mejora |
|----|----------------|----------|--------|
| 220 Hz | −22,2 dB | −38,3 dB | 16,1 dB |
| 880 Hz | −16,1 dB | −32,5 dB | 16,4 dB |
| 1 760 Hz | −12,9 dB | −28,7 dB | 15,8 dB |

### 3.2 Batido por detune (H2), c = 12 cents

| f₀ | Δf predicho | Δf medido |
|----|-------------|-----------|
| 110,00 Hz | 0,765 Hz | 0,765 Hz |
| 146,83 Hz | 1,021 Hz | 1,021 Hz |
| 220,00 Hz | 1,530 Hz | 1,530 Hz |

### 3.3 Delay dub (H3)

Parámetros: T = 0,375 s, fb = 0,65, f_t = 2 200 Hz (a = 0,2691).

| Magnitud | Predicho | Medido |
|----------|----------|--------|
| Razón de áreas entre ecos consecutivos (6 pares) | 0,65 | 0,65 en los 6 |
| Razón de magnitud a f_t entre ecos consecutivos (4 pares) | 0,4615 (−6,72 dB) | 0,4615 en los 4 |
| Repeticiones hasta −60 dB | 17,04 | primer eco bajo −60 dB: el n.º 18 (6,75 s) |
| Estabilidad, fb = 0,95, escala completa, 30 s | acotado en (−1, 1) | máximo 0,7616 (= tanh 1); pico en los últimos 5 s: 0,0004 |

### 3.4 Filtro (H4)

| Q configurada | Ganancia medida en f_c | dB medidos | dB predichos |
|---------------|------------------------|-----------|--------------|
| 0,707 | 0,707 | −3,01 | −3,01 |
| 1,2 | 1,200 | 1,58 | 1,58 |
| 3,0 | 3,000 | 9,54 | 9,54 |
| 6,0 | 6,000 | 15,56 | 15,56 |

Envolvente por defecto (cálculo analítico, no medición): pico 3 959,8 Hz, barrido 7,14 oct/s, cruza 1 kHz a los 0,278 s.

### 3.5 Rugosidad de los voicings (H5, exploratoria)

Rugosidad de interacción (unidades relativas del modelo de Sethares), root D3:

| Modo | Rugosidad |
|------|-----------|
| Off | 0,000 |
| m9 | 0,872 |
| m7 | 0,978 |
| sus m7 | 1,007 |
| m11 | 1,326 |

---

## 4. Discusión

**H1 (confirmada).** PolyBLEP mejora aproximadamente 16 dB en los tres registros. La mejora es constante pero el piso sigue siendo −29 a −38 dB: PolyBLEP de dos puntos no elimina el aliasing, solo lo reduce, y el nivel residual empeora con la altura del tono. Para un instrumento cuyo uso típico está en el registro grave/medio y con un pasa-bajos posterior, es una relación costo/beneficio razonable; para registros agudos habría que evaluar correcciones de mayor orden (Välimäki et al., 2012). Parte del residuo medido también proviene de la propia corrección y de la fuga espectral de la ventana.

**H2 (confirmada).** El batido medido coincide con la fórmula, que además muestra que, a cents fijos, el movimiento es más lento en notas graves (0,765 Hz a 110 Hz): es una propiedad útil para pads graves de dub techno, que evolucionan en periodos de 1 s o más.

**H3 (confirmada).** Las razones medidas coinciden con las predichas al número de decimales reportado. Dos consecuencias de diseño: el decaimiento en Hz es calculable para el productor (con fb = 0,65 y T = 0,375 s, ≈ 6,75 s hasta −60 dB) y la atenuación extra de agudos por repetición (−6,72 dB a f_t) explica el oscurecimiento progresivo del eco. Con fb = 0,95 el sistema sigue estable y la cola es larga (≈ 135 repeticiones hasta −60 dB en régimen lineal); la saturación tanh acota la amplitud y, a escala completa, acelera el decaimiento inicial.

**H4 (confirmada).** La resonancia es directamente la ganancia en la frecuencia de corte, por lo que el control se interpreta en dB (20·log₁₀ Q): el valor por defecto 1,2 aporta +1,58 dB, y el máximo 6 aporta +15,6 dB, que justifica la recomendación de vigilar el nivel de salida en ajustes extremos.

**H5 (exploratoria, interpretación limitada).** La rugosidad calculada es un *proxy* de disonancia sensorial por interacción de parciales, no una medida de preferencia estética. En D3, m9 (voicing sin quinta) resulta menos rugoso que m7, y m11 es el más rugoso. Los resultados dependen del registro y de la composición espectral, y no se validaron con oyentes.

### 4.1 Limitaciones

1. **Modelo de referencia, no binario.** Las mediciones corresponden a una reimplementación en Python. No se ha comparado, muestra a muestra, la salida del plugin C++ contra el modelo (prueba de nulo).
2. **Estado del plugin.** El código C++ no se había compilado ni probado en Ableton Live 12 al escribir este reporte; la compilación y las pruebas en DAW son el siguiente paso (sección 5).
3. **Generador de acordes.** Si el usuario cambia el modo de acorde con una nota sostenida, el note-off se calcula con el modo nuevo y pueden quedar notas colgadas.
4. **Sin limitador de salida.** El nivel por voz (0,22·velocity) deja margen para cuatro notas, pero acordes de cinco notas con resonancia alta pueden superar 0 dBFS; el control `Output` lo compensa.
5. **Cola declarada.** `getTailLengthSeconds()` devuelve 5 s; con fb alto la cola real es mayor.
6. **Versión web.** La demo (`web/index.html`) usa Web Audio: `OscillatorNode` sawtooth (limitado en banda por el navegador), `BiquadFilterNode` pasa-bajos de segundo orden en el filtro y en la realimentación (en vez de TPT/un polo), y una respuesta al impulso sintetizada con `ConvolverNode` (en vez de Freeverb). Es equivalente en intención, no numéricamente.
7. **H5** no tiene validación perceptual.

---

## 5. Conclusiones y trabajo futuro

La arquitectura propuesta es estable y sus etapas se comportan como predice la teoría en el modelo de referencia: el detune produce batidos lentos controlables, PolyBLEP reduce el aliasing en unos 16 dB, el filtro tiene ganancia resonante exacta y el delay dub decae de forma calculable y oscurece cada repetición.

Trabajo pendiente, en orden de prioridad:

1. Compilar el VST3 y cargarlo en Ableton Live 12 (Windows/macOS); registrar versión de JUCE y de compilador.
2. Prueba de nulo: renderizar una secuencia MIDI fija con el plugin y con el modelo Python, y medir el error residual.
3. Medir CPU (porcentaje por voz a 44,1 y 48 kHz) y latencia.
4. Corregir las limitaciones 3 y 5, y evaluar un limitador en la salida.
5. Evaluación perceptual con oyentes (ABX o escala de agrado) para H5 y para el detune por defecto.
6. Interfaz propia (en lugar del editor genérico) y 20–50 presets.

---

## Referencias

- MIDI Manufacturers Association (1996). *MIDI 1.0 Detailed Specification*.
- Plomp, R., y Levelt, W. J. M. (1965). Tonal consonance and critical bandwidth. *Journal of the Acoustical Society of America, 38*(4), 548–560.
- Schroeder, M. R. (1962). Natural sounding artificial reverberation. *Journal of the Audio Engineering Society, 10*(3), 219–223.
- Sethares, W. A. (2005). *Tuning, Timbre, Spectrum, Scale* (2.ª ed.). Springer.
- Välimäki, V., y Huovilainen, A. (2007). Antialiasing oscillators in subtractive synthesis. *IEEE Signal Processing Magazine, 24*(2), 116–125.
- Välimäki, V., Pekonen, J., y Nam, J. (2012). Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation. *Journal of the Acoustical Society of America, 131*(1), 974–986.
- Veal, M. E. (2007). *Dub: Soundscapes and Shattered Songs in Jamaican Reggae*. Wesleyan University Press.
- Zavalishin, V. (2012). *The Art of VA Filter Design*. Native Instruments.
- Zwicker, E., y Fastl, H. (1999). *Psychoacoustics: Facts and Models* (2.ª ed.). Springer.
- W3C. *Web Audio API* (especificación). https://www.w3.org/TR/webaudio/
- JUCE. *JUCE Framework Documentation*. https://juce.com/

## Apéndice: reproducibilidad

```bash
cd validation
python3 validate_dsp.py     # Python 3.12.3, NumPy 2.5.4 → imprime y guarda results.json
```

Todos los valores de la sección 3 provienen de `validation/results.json`, generado por este script.
