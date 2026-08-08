# AoE Clone

Clon de Age of Empires: un wrapper de RTS genérico, construido como motor de estrategia en tiempo real **sin hardcodear mecánicas ni variables de juego**.

## Concepto

En lugar de fijar reglas en el código (costos, vida, producción, atributos...), el motor expone un conjunto de primitivas configurables en tiempo de ejecución:

- **Entidades**: unidades y edificios definidos por datos (JSON/schema), no por clases hardcodeadas
- **Mecánicas**: selección, movimiento, recolección, combate, construcción, producción — implementadas como sistemas desacoplados
- **Configuración**: balance (costos, tiempos, daño, HP) declarativo y editable sin tocar el código
- **Wrapper de RTS**: arquitectura reutilizable para cualquier RTS, no solo este clon

El objetivo es que cambiar cualquier variable de juego (¿cuánta vida tiene un aldeano? ¿cuánto cuesta un caballero?) sea solo editar datos, nunca reescribir lógica.

## Estado actual

Boceto inicial: ventana de pygame con cielo, terreno y HUD. Sin mecánicas todavía.

## Controles

- `ESPACIO`: pausar/reanudar
- `ESC`: salir
- `R`: reiniciar

## Ejecución

```bash
pip install -r requirements.txt
python main.py
```

## Stack

- Python 3
- pygame-ce (renderizado 2D)
