# Rama `regfix`

**Objetivo:** Mejorar el hit registration **sin modificar el sistema de interpolación**.

La interpolación visual a 240-500 Hz se mantiene intacta.

## Enfoque principal

Separar posición visual de posición de colisión:

- El render sigue usando las posiciones interpoladas (máxima fluidez).
- En el momento exacto del cálculo de impacto (proyectiles / raycast / damage), se fuerza temporalmente a los objetos relevantes a usar su posición real del tick (no interpolada).
- Después del cálculo se restaura la posición interpolada.

Así se evita el problema de "se vio que impactó pero no registró" sin sacrificar suavidad visual.

## Base

`main` (sin los cambios de umbrales de la rama `registro`).
