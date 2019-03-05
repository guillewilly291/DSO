/**
    DSO Practica 1, RRF.c
    Descripción: Planificador Round Robin/FIFO con prioridades.

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
void enqueue_adv(TCB* item);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;
// Se declaran las colas que se van a utilizar. De alta prioridad y de baja prioridad.
struct queue * q_hp;
struct queue * q_lp;

/* Initialize the thread library */
void init_mythreadlib() {
  int i;

  // Se inicializan las colas de alta y baja prioridad.
  q_hp = queue_new();
  q_lp = queue_new();

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

  // Se encola el proceso recien creado
  enqueue_adv(&t_state[i]);

  // Si el proceso que se está ejecutando es de baja prioridad y entra en el sistema
  // uno de alta prioridad, este último se activa inmediatamente.
  if(t_state[i].priority == HIGH_PRIORITY && running->priority == LOW_PRIORITY) {
    disable_interrupt();
    activator(scheduler());
  }

  return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();

  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  printf("*** THREAD %i FINISHED\n", tid);

  // Si ninguna de las colas de procesos está vacia se le solicita al
  // planificador el siguiente proceso y se le activa.
  if(queue_empty(q_hp) == 0 || queue_empty(q_lp) == 0){
    disable_interrupt();
    TCB* next = scheduler();
    activator(next);
  }

  printf("*** FINISH \n");

  // El programa termina sin errores aquí.
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
void timer_interrupt(int sig)
{
  // Cada vez que se produce una interrupción de reloj, el proceso que está
  // ejecutandose ve reducido el número de ticks que le quedan para terminar su
  // rodaja.
  running->ticks--;

  // Las rodajas solo se aplican a los procesos de baja prioridad, los de alta
  // prioridad una vez entran en el sistema deben terminar.
  if(running->ticks == 0 && running->priority == LOW_PRIORITY) {
    // Se vuelve a poner el valor de ticks por defecto
    running->ticks = QUANTUM_TICKS;
    disable_interrupt();
    TCB* next = scheduler();
    activator(next);
  }
}

/* Scheduler: returns the next thread to be executed */
TCB* scheduler(){
  // Si el proceso todavía no ha terminado, se le pone al final de la cola.
  if(running->state!=FREE){
    enqueue_adv(running);
  }

  // Se desencola el siguiente proceso de la cola de alta prioridad si existe y
  // si no se desencola el siguiente proceso de la cola de baja prioridad.
  if(queue_empty(q_hp) != 1) {
    return dequeue(q_hp);
  } else {
    return dequeue(q_lp);
  }
  exit(1);
}

/* Activator */
void activator(TCB* next){
  // La variable antiguo ayuda a la hora de realizar el cambio de contexto.
  TCB * antiguo = running;

  //Actualizamos las variables que alamacenan el proceso en ejecución y su tid
  current = next->tid;
  running = next;

  // Hay que diferenciar el cambio de contexto para un hilo que ha terminado,
  // para un hilo que todavía no ha terminado y para cuando un hilo de baja
  // prioridad es expulsado por uno de alta prioridad.
  if(antiguo->state==FREE){
    printf("*** THREAD %i FINISHED: SET CONTEXT OF %i \n", antiguo->tid, next->tid);
    // Hacemos un cambio de contexto mediante set
    if(next->priority == LOW_PRIORITY) enable_interrupt();
    setcontext (&(next->run_env));
  } else if(antiguo->priority == LOW_PRIORITY && running->priority == HIGH_PRIORITY) {
    printf("*** THREAD %i EJECTED: SET CONTEXT OF %i\n", antiguo->tid, running->tid);

    // Hacemos un cambio de contexto mediante swap para que el hilo anterior
    // pueda continuar con su ejecución después.
    if(next->priority == LOW_PRIORITY) enable_interrupt();
    swapcontext (&(antiguo->run_env),&(running->run_env));
  } else {
    printf("*** SWAPCONTEXT FROM %i TO %i\n", antiguo->tid, next->tid);
    // Hacemos un cambio de contexto mediante swap para que el hilo anterior
    // pueda continuar con su ejecución después.
    if(next->priority == LOW_PRIORITY) enable_interrupt();
    swapcontext (&(antiguo->run_env),&(next->run_env));
  }
}

// Función que sirve para encolar sin tener que indicar la cola en la que se
// va a realizar el encolamiento.
void enqueue_adv(TCB* item) {
  if(item->priority == LOW_PRIORITY){
    enqueue(q_lp, item);
  } else {
    enqueue(q_hp, item);
  }
}
