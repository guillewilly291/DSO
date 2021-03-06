#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"


TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
static TCB* running;44444
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

struct queue *q; /*declaramos una cola para los procesos, sean de alta o baja prioridad */ 




/* Initialize the thread library */
void init_mythreadlib() {
  int i;  
  q = queue_new(); 
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
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
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }	

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
 
  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
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
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].ticks= QUANTUM_TICKS; /*asignamos la rodaja que va a tener cada proceso   */
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1); 


  enqueue(q,&t_state[i]); /*Se encolan los procesos a la unica lista, ya que no tenemos en cuenta la prioridad*/
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
void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

	if(!queue_empty(q)){
	   disable_interrupt();
	   TCB * procesoSiguiente = scheduler();
	   activator(procesoSiguiente);
	
	}
  printf("*** FINISH\n");
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


/*  RR para cualquier tipo de proceso, sin importar prioridad*/
TCB* scheduler(){
   /* Si el proceso que se estaba ejecutando no se ha acabado
    encolamos en la lista dicho proceso */
  if(running->state!=FREE){ 
	 enqueue(q,running);	
  }
  /* Si la cola de procesos no esta vacia, desencolamos el primero que haya*/
  if(!queue_empty(q)){
  TCB* procesoSiguiente = dequeue(q);		
  return procesoSiguiente;
  }	
 /* Aqui no deberia llegar, ya que antes de llamar al scheduler se comprueba si la lista esta vacia */
 exit(1);
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
      /* Cada vez que hay un timer_interrupt se reduce la rodaja,
       y se comprueba si se ha terminado esta misma*/
      running->ticks--;
      if (running->ticks ==  0){	/*Si la rodaja ha terminado,se desactivan las interrupciones para proteger una zona critica,
                                    para despues llamar al scheduler y obtener un nuevo proceso */
		      running->ticks = QUANTUM_TICKS;	
	      	disable_interrupt();
	        TCB* next = scheduler();
	      	activator(next);	
      }
} 

/* Activator */
void activator(TCB* next){
  TCB * procesoActual = running;  /* Obtenemos el proceso que esta en marcha, y el que quiere introducirse ( next)*/

  current = next->tid;
  running = next;

	if(procesoActual->state == FREE){ /*Si el proceso en marcha termina imprimimos por pantalla y ponemos el contexto del nuevo */
	printf("*** THREAD %d TERMINATED : SETCONTEXT OF %d\n", procesoActual->tid, next->tid);
	enable_interrupt();
	setcontext (&(next->run_env));
		


	}else{ /*Si el proceso en marcha tendria que seguir su ejecucion, se esta produciendo un cambio de contexto entre dos procesos */
		printf("*** SWAPCONTEXT FROM %d TO %d\n",procesoActual->tid, next->tid);

		enable_interrupt();
	
		if(swapcontext (&(procesoActual->run_env),&(next->run_env))==-1){
			printf("mythread_free: After setcontext, should never get here!!...");
		}		
	}
}



