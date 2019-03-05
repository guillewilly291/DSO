#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include "queue.h"
#include "mythread.h"
#include "interrupt.h"

long hungry = 0L;
int tick_count= 0;
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
struct queue * q;
// Initialize the thread library
// ESTO NO SIRVE PARA ABSOLUTAMENTE NADA TAL Y COMO ESTÁ
void init_mythreadlib() {
  int i;
  //Creamos la cola que alamacenará todos los hilos que se van a ejecutar. Lo creamos aqui ya que esta parte solo se ejecuta una vez
  //Este codigo, copiado de aula global da varios warnings
  q = queue_new ();
  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  //Encolamos el hilo principal
  if(getcontext(&t_state[0].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(5);
  }

  // La N sale de mythread.h
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
  t_state[0].tid = 0;
  running = &t_state[0];
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority){

  int i;

  // Esto solo lo hace una vez, es decir init es 0 solo la primera vez.
  // NO SIRVE PARA NADA
  if (!init) { init_mythreadlib(); init=1;}

  // La N sale de mythread.h
  // TODO: Cambiar el t_state por la cola de hilos.
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;

  if (i == N) return(-1);

  // TODO: Cambiar el t_state por la cola de hilos.
  if(getcontext(&t_state[i].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(-1);
  }

  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].tid = i;
  t_state[i].ticks = QUANTUM_TICKS;
  //Cuando creemos un hilo, lo encolamos.
  enqueue (q , &t_state[i]);
  // TODO: Entender que narices es esto, aunque tampoco es importante
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;

  // TODO: Confirmar que es lo que hace el cambio de contexto
  makecontext(&t_state[i].run_env, fun_addr, 1);

  return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();

  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  printf ("El hilo %i ha acabado \n", tid);
  TCB* next =  dequeue(q);
  running = next;
  printf ("El siguiente hilo es el hilo %i \n", next->tid);
  queue_print(q);
  //printf("TICKS: %i\n", t_state[tid].ticks);
  activator(next);
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
tick_count++;
printf("tick_count: %i\n", tick_count );
if(tick_count == 200){
  tick_count=0;
  activator(scheduler());

}
}

/* Scheduler: returns the next thread to be executed */
//Creo que para RondRobin puro no necesitamos planificador, se van cambiando por el orden de la cala
TCB* scheduler(){
  // int i;
  // for(i=0; i<N; i++){
  //     if (t_state[i].state == INIT) {
  //       current = i;
  //       printf("TICKS: %i\n", t_state[i].ticks);
	//       return &t_state[i];
	//     }
  // }
    int tid = running->tid;
    printf("El TID actual y el que se va a encolar es el %i \n", tid);
    t_state[tid].state = FREE;
    free(t_state[tid].run_env.uc_stack.ss_sp);
    enqueue (q , &t_state[tid]);
    TCB* next =  dequeue(q);
    running = next;
    // printf("El siguiente es %p \n", (void*) &next);
    queue_print(q);
    return next;


  printf("mythread_free: No thread in the system\nExiting...\n");
  exit(1);
}

/* Activator */
void activator(TCB* next){
	setcontext (&(next->run_env));
	printf("mythread_free: After setcontext, should never get here!!...\n");
}
