/**
    DSO Practica 1, RR.c
    Descripción: Planificador Round Robin

    @author Juan Abascal
    @author Daniel Gonzalez
    @date 17/03/17
*/
#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"
#include "queue.h"

long hungry = 0L;

TCB* scheduler();
void activator();
void timer_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;
// Se declara la cola que se va a utilizar
struct queue * q;

/* Initialize the thread library */
void init_mythreadlib() {
  int i;

  // Inicializamos la cola que se va a utilizar durante el programa al inicializar mythreadlib.
  q = queue_new();

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(5);
  }

  // Detección de errores en QUANTUM TICKS
  if(QUANTUM_TICKS < 0) {
    printf("ERROR: QUANTUM TICKS have to be greater than zero\n");
    exit(-1);
  }

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;
  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].ticks = QUANTUM_TICKS;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);

  // Encolamos el proceso que se acaba de crear
  enqueue(q, &t_state[i]);

  return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();

  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  printf("*** THREAD %i FINISHED\n", tid);

  // Se comprueba si queda algún proceso en la cola, si es así se llama al
  // scheduler para que nos devuelva el siguiente proceso que hay que
  // ejecutar y este se llama a la función activator para ejecutar ese proceso.
  // Si ya no quedan hilos en la cola, el programa termina.
  if(queue_empty(q) == 0){
    disable_interrupt();
    TCB* next = scheduler();
    activator(next);
  }

  printf("*** FINISH \n");

  // Indicamos que el programa se ha terminado sin errores
  exit(0);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Timer interrupt  */
void timer_interrupt(int sig){
  // Cada vez que se produce una interrupción de reloj, el proceso que está
  // ejecutandose ve reducido el número de ticks que le quedan para terminar su
  // rodaja.
  running->ticks--;

  // Cuando se termina el tiempo de procesador disponible para el proceso en
  // ejecución se cambia al siguiente proceso.
  if(running->ticks == 0) {
    running->ticks = QUANTUM_TICKS;
    disable_interrupt();
    TCB* next = scheduler();
    activator(next);
  }
}

/* Scheduler: returns the next thread to be executed (ROUND ROBIN) */
TCB* scheduler(){
  // Si el proceso todavía no ha terminado, se le pone al final de la cola.
  if(running->state!=FREE){
    enqueue(q, running);
  }

  // El siguiente proceso es aquel que está el primero en la cola.
  TCB* next = dequeue(q);
  return next;
  exit(1);
}

/* Activator */
void activator(TCB* next){
  // La variable antiguo ayuda a la hora de realizar el cambio de contexto.
  TCB * antiguo = running;

  //Actualizamos las variables que alamacenan el proceso en ejecución y su tid
  current = next->tid;
  running = next;

  // Hay que diferenciar el cambio de contexto para un hilo que ha terminado y
  // para un hilo que todavía no ha terminado.
  if(antiguo->state==FREE){
    printf("*** THREAD %i FINISHED: SET CONTEXT OF %i \n", antiguo->tid, current);

    // Hacemos un cambio de contexto mediante set porque el hilo anterior ya
    // ha terminado.
    enable_interrupt();
    setcontext (&(next->run_env));
  } else {
    printf("*** SWAPCONTEXT FROM %i TO %i\n", antiguo->tid, current);

    // Hacemos un cambio de contexto mediante swap para que el hilo anterior
    // pueda continuar con su ejecución después.
    enable_interrupt();
    swapcontext (&(antiguo->run_env),&(next->run_env));
  }
}
