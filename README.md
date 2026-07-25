# AABB Automata

Simulador de combate aereo 2D autónomo. Aviones con IA que pelean entre sí.

## Concepto

Cada avión es un agente autónomo que opera con:

- **Sensores**: raycasts que detectan enemigos, misiles y el limite del mapa
- **Armamento**: missiles balísticos con追踪
- **Propulsión**: movimiento con aceleracion y rotacion
- **Vida**: barra de vida, muere al recibir suficiente daño

Los aviones usan **AABB** (Axis-Aligned Bounding Box) para detección de colisiones.

## Controles

- `ESPACIO`: pausar/reanudar
- `ESC`: salir
- `R`: reiniciar batalla

## Ejecución

```bash
pip install pygame-ce
python main.py
```

## Stack

- Python 3
- pygame-ce (renderizado 2D)
