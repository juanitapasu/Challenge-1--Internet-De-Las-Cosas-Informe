# Challenge-1 Informe
## 4. Justificación de la medición de Amoníaco (NH₃)

### 4.1 Caracterización del gas NH₃

El amoníaco (NH₃) es un gas incoloro con un olor intenso y acre, ampliamente utilizado en entornos industriales y agrícolas. Se emplea en:

- Producción de fertilizantes
- Industria alimentaria (refrigeración y almacenamiento)
- Procesos agrícolas
- Productos de limpieza
- Fabricación de explosivos y fármacos

En condiciones de alta temperatura, puede descomponerse formando óxidos de nitrógeno (NOx), los cuales también son contaminantes atmosféricos.

Según organismos regulatorios como OSHA y ACGIH, el NH₃ es considerado un gas:

- Inflamable (difícil de encender)
- Extremadamente tóxico a altas concentraciones
- Irritante para ojos y vías respiratorias

---

### 4.2 Justificación ambiental en Sabana Centro

Se decidió monitorear NH₃ debido a:

#### 1️ Actividad agropecuaria

La región Sabana Centro presenta alta actividad agrícola, donde el uso de fertilizantes nitrogenados genera emisiones de amoníaco al ambiente.

#### 2️ Procesos industriales

Zonas industriales pueden liberar amoníaco en refrigeración y almacenamiento de alimentos.

#### 3️ Formación secundaria de material particulado

El NH₃ reacciona en la atmósfera con compuestos ácidos formando partículas finas (PM2.5), lo cual contribuye indirectamente al aumento del material particulado respirable.

Por lo tanto, aunque el prototipo no mida directamente PM2.5 en esta fase, el monitoreo de NH₃ permite identificar condiciones que favorecen su formación.

---

### 4.3 Tabla de clasificación de niveles de NH₃

A partir de los límites regulatorios internacionales (OSHA, ACGIH, IDLH) y los efectos documentados en humanos, se propone la siguiente tabla de clasificación para el sistema IoT.

> Valores expresados en ppm (partes por millón)

| Rango (ppm) | Clasificación | Nivel de Riesgo | Efectos en Salud | Acción del Sistema IoT |
|-------------|--------------|----------------|------------------|------------------------|
| 0 – 25 ppm | Aire Aceptable | Bajo | Irritación leve en ojos y tracto respiratorio | Visualización normal en LCD |
| 25 – 50 ppm | Precaución | Moderado | Límite permisible OSHA (PEL = 50 ppm TWA) | Advertencia visual (mensaje preventivo) |
| 50 – 100 ppm | Riesgo Alto | Alto | Hinchazón de párpados, vómitos, irritación severa | Activación de alerta visual y sonora |
| 100 – 500 ppm | Crítico | Muy Alto | Riesgo grave, exposición prolongada puede ser mortal | Activación inmediata de alarma sonora continua |
| > 500 ppm | Peligro Inminente | Extremo | IDLH (Inmediatamente peligroso para la vida) | Alarma máxima y señal crítica |

---

### 4.4 Justificación de la selección del NH₃ en el prototipo

La elección del amoníaco como variable principal de contaminación se fundamenta en:

#### ✔ Disponibilidad tecnológica

El sensor MQ-135 permite la detección de NH₃ en rangos bajos y es compatible con el ESP32 mediante lectura analógica.

#### ✔ Bajo costo

El MQ-135 es económicamente accesible, cumpliendo con la restricción del reto de desarrollar un sistema IoT de bajo costo.

#### ✔ Impacto en salud pública

Concentraciones superiores a 25 ppm ya generan efectos fisiológicos medibles. A partir de 50 ppm se alcanza el límite permisible de exposición laboral según OSHA.

#### ✔ Relevancia ambiental regional

En zonas agrícolas y periurbanas, el NH₃ es un contaminante frecuente y precursor de partículas finas.

#### ✔ Aplicabilidad comunitaria

El monitoreo in situ permite advertir a comunidades cercanas a zonas agrícolas o industriales sobre incrementos peligrosos.

---

### 4.5 Integración con lógica de alerta del sistema

El sistema implementa la siguiente lógica:

- Si NH₃ ≤ 25 ppm → Estado: “Aire Aceptable”
- Si 25 < NH₃ ≤ 50 ppm → Estado: “Precaución”
- Si 50 < NH₃ ≤ 100 ppm → Estado: “Alerta Alta”
- Si NH₃ > 100 ppm → Estado: “Peligro”

En niveles superiores a 50 ppm se activa buzzer.  
En niveles superiores a 100 ppm la alarma es continua.

---

### 4.6 Relación con criterios de diseño ingenieril

La selección del NH₃ cumple con:

- Restricciones económicas (sensor de bajo costo)
- Restricciones técnicas (integración simple con ESP32)
- Impacto en salud pública
- Enfoque ambiental regional
- Sistema autónomo sin red

Esto demuestra aplicación del diseño ingenieril bajo limitaciones reales, alineado con la rúbrica del Challenge.
