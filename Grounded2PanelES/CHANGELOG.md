# Cambios

## 0.3.0 — Actualización para Grounded 2 0.6.0 («Rot and Ruin»)

**Compatibilidad con actualizaciones**
- Ya no depende de offsets fijos de una versión concreta: localiza en tiempo de ejecución la tabla de objetos de Unreal, la tabla de nombres (con decodificador propio) y verifica el índice de `ProcessEvent`.
- Cada función del juego que usa el panel se busca por nombre y se comprueba (parámetros, tipos y tamaños) antes de llamarla. Lo que cambió en una actualización queda desactivado con el motivo, en vez de arriesgar la partida.
- Si cambia el propio motor Unreal, el panel se pone en pausa sin tocar nada.
- Detecta todos los nombres del ejecutable de Steam (`Grounded2Steam-Win64-Shipping.exe`, `Grounded2-WinGRTS-Shipping.exe`…).
- Funciona tras cambiar de partida sin reiniciar el juego; se puede cargar desde el menú principal.

**Interfaz nueva**
- Ventana moderna con Dear ImGui y Direct3D 11: barra lateral con iconos, tarjetas, interruptores animados, notificaciones, confirmaciones, tipografías Nunito/Fredoka e iconos Font Awesome.
- Tecla configurable (F8 por defecto), siempre encima, opacidad y tamaño de la interfaz.
- Página de Registro y detalle de compatibilidad.

**Iniciador nuevo (IniciarPanel.exe)**
- Abre el juego por Steam, espera a que esté listo y carga el panel; `--auto` para usarlo como herramienta de Vortex.
- Copias de seguridad automáticas de las partidas (se guardan las 15 últimas).
- Empaquetado con la carpeta `Augusta`, así Vortex lo despliega en la carpeta del juego.

**Funciones nuevas**
- Jugador: modo dios, resistencia infinita, sin hambre ni sed, restauración total, velocidad, salto, vuelo y atravesar paredes.
- Objetos: favoritos, recientes, dar a otros jugadores y kits rápidos.
- Mundo: fijar/bloquear la hora, velocidad del tiempo, regenerar recursos, terminar asedios, ocultar HUD y todas las reglas de partida personalizada.
- Teletransporte: puntos guardados, volver atrás, avanzar, ir con un jugador o traerlo, traer al grupo.
- Criaturas: domar, reanimar montura, generar y montar, aracnofobia.
- Progreso: desbloqueos (recetas, árboles tecnológicos, temas SCA.B, mapa), omniherramienta, reparar equipo, recuperar mochilas.
- Construcción: integridad estructural y resistencia de las construcciones.

**Se conserva de la 0.2.0**: daño por alcance (solo yo / un jugador / mundo), daño enemigo, recogida automática con comprobación de mochila, eliminar objetos cercanos con confirmación, molares, ciencia, puntos de montura, curación, reanimación, resistencia, avanzar hora, mutaciones, nombres en español y restaurar ajustes de sesión.

## 0.2.0
Daño causado por alcance, recogida automática, limpieza cercana y utilidades (molares, ciencia, monturas, salud, resistencia y hora). Steam 0.5.1.1.
