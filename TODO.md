# PRIMER CHECKPOINT

- [x] Crear los proyectos para los diversos procesos del trabajo práctico y sincronizarlos al repositorio de Git.

- [X] Desarrollar un servidor multicliente en el Kernel y clientes simples en el resto de los procesos. 

- [X] Cada proceso deberá leer su configuración inicial desde su archivo de configuración, en la que contará con los parámetros de conexión.

- [X] Al iniciar, cada proceso informará por pantalla su configuración, hará un Handshake con los procesos con quienes deba conectarse y luego quedarán a la espera de recibir datos.

- [X] Desde la consola podrá enviarse un mensaje con tamaño máximo fijo al Kernel, el cual deberá imprimir por pantalla el texto ingresado en la misma, y replicarlo a la Memoria, CPU y FileSystem. Estos últimos también deberán mostrar el contenido del mensaje por pantalla.

# SEGUNDO CHECKPOINT

- [x] El sistema anterior ahora permite enviar archivos a través de un comando enviado por el proceso Consola, de tal forma que sean almacenados en el proceso Memoria y sean accesibles desde el Kernel y las CPU. Dichos archivos serán de tamaño variable.

- [x] El kernel a este checkpoint tan solo permitirá la ejecución de un proceso muy simple que se creará en el momento de enviar el código desde la consola. Se creará un pequeño PCB con un ID de proceso y un contador de páginas para dicho PCB. Al enviar los datos a la Memoria, y obtener el OK de la misma, se aumentará el contador de páginas.

- [x] Si bien el Kernel debe permitir la conexión de múltiples consolas, debe validar que no exista más de una consola enviando datos en simultáneo. Para ello, se fijará un nivel de multiprogramación en 1 en el archivo de configuración.

- [x] Al inicializar el proceso Memoria a este checkpoint, reservará un gran bloque de datos de tamaño configurable que será visto como un solo Frame, obteniendo tan solo una gran página. Esto le permitirá al Kernel leer y escribir bytes en dicho frame, utilizando la API definida por el enunciado. Este proceso deberá permitir, a través de su consola, hacer un dump del único frame utilizando el comando dump.

- [x] Por otro lado, deberá poder conectarse un proceso CPU a través de un handshake al Kernel, y se deberá incorporar al mismo la biblioteca del parser. Se visualiza ya la implementación de algunas instrucciones básicas de ANSISOP.

# TERCER CHECKPOINT

- [x] El proceso Consola ahora reconoce todos los comandos propuestos en el enunciado. El objetivo de este checkpoint es tener una aplicación que ejecute programas simples.

- [x] A la hora de recibir un nuevo proceso, el Kernel almacenará las páginas de dicho proceso en la Memoria y guardará los valores de las mismas en el PCB, inicializando el Program Counter y cargando los datos del los índices provistos por el parser. Sumado a esto, el PCB deberá ahora contener datos del Exit Code del programa, que podrá ser 0 o un valor negativo dependiendo si la consola finalizó su conexión antes de tiempo. Además, el Kernel permitirá obtener el listado de procesos del sistema, finalizar un proceso, consultar su estado y detener la planificación.

- [X] Además, existirá una regla fija ante la syscall write que permite enviar a imprimir texto a la consola siempre y cuando el FD sea 1. Caso contrario, el sistema deberá fallar.

- [x] La Memoria ya se encuentra dividida en Frames de tamaño configurable. Se creará la estructura de paginación inversa, que asociará un Frame, una Página y un PID. Por el momento no se requiere que la estructura de páginas se encuentre en memoria principal. Deberá atender, además, las peticiones de memoria de nuestra CPU, por lo que ya se visualiza una arquitectura multihilo. Para evitar problemas de sincronización en una etapa temprana, es válido atender los pedidos de forma secuencial.

- [x] El proceso CPU ya puede conectarse con la Memoria y realizar pedidos de lectoescritura. Además, el CPU permite ahora ejecutar una ráfaga de ciclos de instrucción al recibir un PCB del Kernel. Para ello, ahora puede manejar la estructura del Stack. Si bien no se requiere que se implementen todas las instrucciones, es menester la implementación de la mayoría de las operaciones no privilegiadas.

# CUARTO CHECKPOINT

> El objetivo de este checkpoint es soportar la utilización de varias operaciones privilegiadas sobre memoria.

- [x] La consola comienza a llevar la información estadística requerida en el enunciado.

- [x] El Kernel ya puede trabajar con múltiples programas, permitiendo indicar su nivel de multiprogramación por archivo de configuración. Debido a esto, comienzan a vislumbrarse los estados de ejecución de un programa. Se requiere, cuánto mínimo, una cola de New que mantenga el grado de multiprogramación, una cola de Ready, una cola de Exec que mande a ejecutar un programa a una única CPU, y una cola de Exit que permite mantener la información de finalización de un programa. El sistema de planificación será FIFO.

- [X] Se crea en el Kernel la Capa de Memoria, inicializando las variables compartidas y soportando las operaciones con semáforos de ANSISOP, junto con las estructuras administrativas necesarias para que puedan utilizarse. Se sincroniza correctamente el acceso a las mismas.

- [X] Además, se soportan ahora las syscall sobre las operaciones de sincronización de ANSISOP y sus variables compartidas.

- [x] La memoria ya contiene un hilo por conexión y permite acceso en simultáneo a las estructuras del sistema.

- [X] El proceso Filesystem deberá poder reconocer estructuras de árboles de directorios y archivos. Podrá responder una solicitud simple del Kernel como la existencia de un archivo o su tamaño.

# QUINTO CHECKPOINT

> El objetivo de este checkpoint es tener un programa que permite ejecutar varios programas en forma paralela, 
incorporando las estructuras del FileSystem y una caché que deberá aumentar ampliamente su performance.


- [x] El Kernel ya permite ejecutar varios programas en paralelo. Se debe prestar especial atención a la hora de sincronizar la aplicación para soportar las transiciones de múltiples programas.

- [ ] Se amplía la capa de Memoria del Kernel, encargada de administrar las syscalls que requieren acceso a la creación de memoria dinámica del programa (malloc).

- [X] Se crea la capa de FileSystem en el Kernel, encargada de manejar la tabla de archivos por proceso y tabla de archivos globales. Se implementan las syscalls de la CPU que permiten abrir y cerrar archivos, junto con los comandos de la consola del kernel que permiten manejar dichas estructuras.

- [x] Se crea la estructura de caché para la memoria.

- [X] Las CPUs ahora corren en forma paralela. Además, se implementan las operaciones que requieren acceso al heap de los programas.

# ENTREGA FINAL

> Finalización del trabajo práctico, se itera por última vez para obtener una versión de TP con todos los módulos
funcionando.

- [X] Se incorporan al Kernel los algoritmos de planificación a la hora de elegir qué procesos ejecutar. Además, se implementan permisos sobre los archivos que se encuentran abiertos, validando todos los casos de errores posibles. El Kernel ya mantiene información estadística de los programas a la hora de ejecutarlos.

- [X] La CPU ya puede ejecutar instrucciones que requieren acceso a archivos, completando todas las instrucciones del sistema. Se permite la desconexión de los procesos CPU en tiempo de ejecución, y se maneja de forma acorde el error.

- [X] Se implementan las operaciones que el enunciado propone desde la consola de la Memoria para manejar la caché.

- [X] El FileSystem ya permite obtener los datos de un archivo y escribir sobre el mismo a través de un mensaje del Kernel. Esto permite al Kernel y la CPU implementar las Syscalls que leen y escriben sobre archivos. Además, se permite borrar archivos desde la syscall de ANSISOP.

# TODO LIST DEL EQUIPO

> En caso que el equipo quiera crear una meta, se puede editar acá.

- [x] Hacer un branch para tener el código funcionando

- [x] Sufrir una crisis existencial

- [ ] Aprobar el TP xD
