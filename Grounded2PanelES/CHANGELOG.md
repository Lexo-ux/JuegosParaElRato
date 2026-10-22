# Cambios

## 0.3.3 — Formato de elemento de Grounded 2 0.6.0.3

- El volcado del registro de 0.3.2 mostró el cambio: en 0.6.0.3 cada elemento de la tabla de objetos empieza con una palabra de indicadores (`RootSet`…) y el puntero al objeto está 8 bytes después. El panel detecta ahora la posición del puntero dentro del elemento (+0, +8 o +0x10).
- El motor simulado prueba las cuatro combinaciones (plana o por bloques, puntero en +0 o +8).

## 0.3.2 — Tabla de objetos de Grounded 2 0.6.0.3

- Con Grounded 2 0.6.0.3 el panel se quedaba en «Buscando las estructuras internas del juego» porque no reconocía la tabla de objetos; esta versión amplía la búsqueda (pendiente de confirmar en el juego real).
- La tabla de objetos se busca primero por firmas de código y después en las secciones del ejecutable, y se aceptan los dos formatos de Unreal 5 (plano y por bloques) y varios tamaños de elemento.
- Si aun así no se encuentra, el registro incluye un volcado de los mejores candidatos para poder corregirlo.
- El motor simulado de pruebas cubre ahora los dos formatos.

## 0.3.1 — Detección del juego

- El iniciador detecta el juego aunque cambie el nombre del ejecutable (por proceso y por la ventana de Unreal) y avisa si el juego se ejecuta como administrador y el iniciador no.
- Nuevo botón «Copiar diagnóstico» en el iniciador: Windows, proceso, ventana, permisos, instalación y el final del registro del panel, listo para pegar.
- El panel explica en pantalla y en el registro por qué no termina de localizar el juego (tabla de objetos, de nombres, controlador o estado del jugador).
- Si el controlador del jugador no genera actividad, el panel se engancha también a la HUD para no quedarse en «Esperando partida».

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
