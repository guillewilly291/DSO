#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB *scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];

/* Current running thread */
static TCB *running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function()
{
  while (1);
}

struct queue *low; /*declaramos una cola para los procesos de alta prioridad*/
struct queue *high;/*declaramos una cola para los procesos de baja prioridad*/

/* Initialize the thread library */
void init_mythreadlib()
{
  int i;
  low = queue_new();/*inicializamos las respectivas colas*/
  high = queue_new();

  /* Create context for the idle thread */
  if (getcontext(&idle.run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if (idle.run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1);

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if (getcontext(&t_state[0].run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }

  for (i = 1; i < N; i++)
  {
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();
}

/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create(void (*fun_addr)(), int priority)
{
  int i;
	
  if (!init)
  {
    init_mythreadlib();
    init = 1;
  }
  for (i = 0; i < N; i++)
    if (t_state[i].state == FREE)
      break;
  if (i == N)
    return (-1);
  if (getcontext(&t_state[i].run_env) == -1)
  {
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].ticks = QUANTUM_TICKS;/*asignamos el tamaño de la rodaja a cada thread que se crea*/
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if (t_state[i].run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);

  if (t_state[i].priority == LOW_PRIORITY)/*si la prioridad es baja, encolamos en la cola de baja prioridad*/
  {

    enqueue(low, &t_state[i]);
  }
  else if(t_state[i].priority != HIGH_PRIORITY)/*si la prioridad es alta, encolamos en la cola de alta prioridad*/
  {
     enqueue(high, &t_state[i]);
  }else{/*si la prioridad es otra---error*/
      perror("No se deben introducir otro tipo de prioridades");
      exit(1);
  }
   

   
  
  /* si se estan creando los threads y se empieza a ejecutar uno de baja prioridad 
 y de repente aparece uno de alta, hay que cambiarlo  
*/
  if (t_state[i].priority == HIGH_PRIORITY && running->priority == LOW_PRIORITY)
  {
    	disable_interrupt();
	    TCB *procesoSiguiente = scheduler();
    	activator(procesoSiguiente);
  }

  return i;

} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
  return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
}

/* Free terminated thread and exits */
void mythread_exit()
{
  int tid = mythread_gettid();

  printf("*** THREAD %d FINISHED\n", tid);
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  if (!queue_empty(high) || !queue_empty(low))/*si alguna de las colas tiene procesos llamamos al planificador y al activador con el proceso que este ultimo nos devuelva*/
  {
    
    disable_interrupt();
    TCB *procesoSiguiente = scheduler();
    activator(procesoSiguiente);
  }
  printf("*** FINISH\n");
  exit(0);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority)
{
  int tid = mythread_gettid();
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority)
{
  int tid = mythread_gettid();
  return t_state[tid].priority;
}

/* Get the current thread id.  */
int mythread_gettid()
{
  if (!init)
  {
    init_mythreadlib();
    init = 1;
  }
  return current;
}

/* FIFO para alta prioridad, RR para baja*/
TCB *scheduler()
{
/* Si el proceso que se estaba ejecutando no se ha acabado
    encolamos en la lista dicho proceso */
  if (running->state != FREE)
  {
   /*si el proceso en ejecucion es de baja prioridad lo encolamos en la de baja prioridad-- realmente uno de alta prioridad jamas se encolaria de nuevo*/
    if(running->priority == LOW_PRIORITY)
    {     
      enqueue(low, running);     
    }
  }
	/*obtenemos un nuevo proceso siempre que las colas no esten vacía, intententando siempre recuperar uno de alta prioridad primero*/
  if (!queue_empty(high) || !queue_empty(low))
  {
    TCB *procesoSiguiente;
    if (!queue_empty(high))
    {    
       procesoSiguiente = dequeue(high);
    }
    else
    {	
        procesoSiguiente = dequeue(low);	
    }

    return procesoSiguiente;
  }
  exit(1);
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
  running->ticks--; /* Reducimos la rodaja en 1*/
  if (running->ticks == 0 && running->priority == LOW_PRIORITY)/*Si la rodaja ha terminado y el proceso actual es de prioridad baja,se desactivan las interrupciones para proteger una zona critica,
                                    para despues llamar al scheduler y obtener un nuevo proceso */
  {
    running->ticks = QUANTUM_TICKS;
    disable_interrupt();
    TCB *next = scheduler();
    activator(next);
  }
}

/* Activator */
void activator(TCB *next)
{
  TCB *procesoActual = running;

  current = next->tid;
  running = next;

  if (procesoActual->state == FREE) /*Si el proceso en marcha termina imprimimos por pantalla y ponemos el contexto del nuevo */
  {
    printf("*** THREAD %d TERMINATED : SETCONTEXT OF %d\n", procesoActual->tid, next->tid); 
    if (next->priority == LOW_PRIORITY)/* en  caso de que sea de baja prioridad el nuevo, reactivamos las interrupciones, en caso de alta no, pues se tiene que ejecutar hasta su fin */
    {
      enable_interrupt();
    }
    setcontext(&(next->run_env));/* se pone el contexto del nuevo*/
  }
  else
  {
    if (procesoActual->priority == LOW_PRIORITY && next->priority == HIGH_PRIORITY)/*si el thread actual es de baja y el siogoente es de alta imprimimos un mensaje, y si son los dos baja, uno distinto*/
    {

      printf("*** THREAD %d PREEMTED : SETCONTEXT OF %d\n", procesoActual->tid, next->tid);
    }
    else
    {

      printf("*** SWAPCONTEXT FROM %d TO %d\n", procesoActual->tid, next->tid);
    }

    if (next->priority == LOW_PRIORITY)/* en  caso de que sea de baja prioridad el nuevo, reactivamos las interrupciones, en caso de alta no, pues se tiene que ejecutar hasta su fin */
    {
      enable_interrupt();
    }

    if (swapcontext(&(procesoActual->run_env), &(next->run_env)) == -1)
    {
      printf("mythread_free: After setcontext, should never get here!!...");
    }
  }
}
