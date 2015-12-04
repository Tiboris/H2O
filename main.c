/**************************
**	Project	Building H20 **
**	Author	Tibor Dudlák **
***************************/
/******************************************************** LIBRARIES *************************************************************/
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <semaphore.h>
#include  <sys/ipc.h>
/******************************************************** CONSTANTS *************************************************************/
#define K 1000
#define ENOUGH 3
#define OPT_COUNT 5
#define MAX_TIME 5000
#define ERR_SYS_CALL 2
/********************************************************* STRUCTS **************************************************************/
typedef struct 
{
	sem_t oxy;
	sem_t write;
	sem_t hydro;
	sem_t mutex;
	sem_t finish;
	sem_t barrier;
	sem_t bonding;
	sem_t wait_to_bond;
	sem_t bonding_barrier;
	long long int oxygens, hydrogens, shared_counter, N, already_bonded, to_bond, to_bond2, oxygen_counter, hydrogen_counter;
	long int max_sleep_hydro, max_sleep_oxy, max_sleep_bond;
	FILE* out;
} To_share;
/******************************************************** VARIABLES *************************************************************/
To_share *data;
int memory_id = 0;
/******************************************************** FUNCTIONS *************************************************************/
void bond();
void oxygen();		
bool prepare();
void release();
void hydrogen();
void wait_bonding();
void wait_for_all();
void start_parent();
bool clear_memory();
void print_arg_err();
void print_sys_err();
void interupt_signal();
void ready(char who, long long int id);
void not_ready(char who, long long int id);
void make_atoms(int atom, long long int count);
bool parse_args( int argc, char const *argv[] );
void do_write(char atom, char* operation, long long int type);
/********************************************************** MAIN ****************************************************************/
int main( int argc, char const *argv[] )
{
	if ( (! prepare ()) || ( ( data->out = fopen("h2o.out","w") ) == NULL ) ) return ERR_SYS_CALL;
	if ( ! parse_args ( argc, argv ) ) return EXIT_FAILURE;
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = interupt_signal;
   	sigemptyset(&sigIntHandler.sa_mask);
   	sigIntHandler.sa_flags = 0;
   	sigaction(SIGINT, &sigIntHandler, NULL);
	int child;
	pid_t pid, my_child[2];
	for (child = 0; child < 2; ++child)
	{
		pid = fork();
		if(pid == 0)
			break;
		else if (pid > 0)
		{
			my_child[child] = pid;
		}
	}
	if(pid == 0) (child == 0) ? make_atoms(child, data->N) : make_atoms(child, data->N * 2);
	if( pid > 0)
	{
		waitpid(my_child[0],NULL,0);
		waitpid(my_child[1],NULL,0);
		if (!clear_memory()) return ERR_SYS_CALL;
	}
	return 0;
}
/********************************************************************************************************************************/
void make_atoms(int atom, long long int count)
{
	pid_t pids[count],pid=getpid();
	for (int i = 0; i < count; ++i)
	{
		pids[i]=fork();
		if ( pids[i] == 0 )
		{
			( atom ) ? hydrogen() : oxygen();
			break;
		}
		else
		{
			( atom ) ? usleep(rand()%(((data->max_sleep_hydro+1)-0)+0)) : usleep(rand()%(((data->max_sleep_oxy+1)-0) +0));
		}
	}
	if ( pid > 0 )
	{
		for (int i = 0; i < count; ++i)
		{
			waitpid(pids[i],NULL,0);	
		}
	}
	return;
}
void ready(char who, long long int id)
{
	do_write(who,"ready", id);
	sem_post(&data->hydro);
	sem_post(&data->hydro);
	data->hydrogens-=2;
	sem_post(&data->oxy);
	data->oxygens--;
	return;	
}
void not_ready(char who, long long int id )
{
	do_write(who,"waiting",id);
	sem_post(&data->mutex);
	return;
}
void hydrogen()
{
	++data->hydrogen_counter;
	long long int id=data->hydrogen_counter;
	do_write('H',"started", id );
	sem_wait(&data->mutex);
	data->hydrogens++;
	( data->hydrogens> 1 && data->oxygens > 0 ) ? ready('H', id) : not_ready ('H', id);
	sem_wait(&data->hydro);
	do_write('H',"begin bonding", id );
	bond();
	do_write('H',"bonded", id );
	wait_bonding();
	wait_for_all();
	do_write('H',"finished",id );
}
void oxygen()
{
	++data->oxygen_counter;
	long long int id=data->oxygen_counter;
	do_write('O',"started", id);
	sem_wait(&data->mutex);
	data->oxygens++;
	( data->hydrogens > 1 ) ? ready('O', id) : not_ready ('O', id);
	sem_wait(&data->oxy);
	do_write('O',"begin bonding",id);
	bond();
	do_write('O',"bonded",id);
	wait_bonding();
	sem_post(&data->mutex);
	wait_for_all();
	do_write('O',"finished", id);
}
void bond()
{
	usleep(rand()%(((data->max_sleep_bond+1)-0)+0));
	sem_wait(&data->bonding);
	data->to_bond++;
	if (data->to_bond == ENOUGH)
	{
		data->to_bond = 0;
		sem_post(&data->wait_to_bond);
		sem_post(&data->wait_to_bond);
		sem_post(&data->wait_to_bond);
	}
	sem_post(&data->bonding);
	sem_wait(&data->wait_to_bond);
	return;
}
void wait_bonding()
{
	sem_wait(&data->bonding);
	data->to_bond2++;
	if (data->to_bond2 == ENOUGH)
	{
		data->to_bond2 = 0;
		sem_post(&data->bonding_barrier);
		sem_post(&data->bonding_barrier);
		sem_post(&data->bonding_barrier);
	}
	sem_post(&data->bonding);
	sem_wait(&data->bonding_barrier);
	return;
}
void wait_for_all()
{
	sem_wait(&data->finish);
	data->already_bonded++;
	sem_post(&data->finish);
	if (data->already_bonded == ENOUGH*data->N) 
	{
		sem_post(&data->barrier);
	}
	sem_wait(&data->barrier);
	sem_post(&data->barrier);
	return;
}
void do_write(char atom, char* operation, long long int type)
{
	sem_wait(&data->write);
	fprintf(data->out,"%llu\t: %c %llu\t: %s\n", data->shared_counter++, atom, type, operation );
	fflush(data->out);
	sem_post(&data->write);
	return;
}
bool prepare()
{
	if ( (memory_id = shmget(IPC_PRIVATE, sizeof(To_share), IPC_CREAT | 0666) ) == -1 ) 
	{
		print_sys_err();
		return false;
	}
	data = shmat(memory_id, (void*) 0,0);
	if  (
			( sem_init(&data->bonding_barrier,1,0) == -1 )		||
			( sem_init(&data->wait_to_bond,1,0) == -1 )			||
			( sem_init(&data->barrier,1,0) == -1 )				||
			( sem_init(&data->bonding,1,1) == -1 )				||
    		( sem_init(&data->finish,1,1) == -1 ) 				||
    		( sem_init(&data->hydro,1,0) == -1 ) 				||
    		( sem_init(&data->mutex,1,1) == -1 ) 				||
    		( sem_init(&data->write,1,1) == -1 )				||
    		( sem_init(&data->oxy,1,0) == -1 )
    	)  return false;
	data->shared_counter = 1;
	data->oxygens = 0;
	data->oxygen_counter = 0;
	data->hydrogens = 0;
	data->hydrogen_counter = 0;
	data->already_bonded = 0;
	data->to_bond = 0;
	data->to_bond2 = 0;
	return true;
}
bool clear_memory()
{
	fclose(data->out);
	if  (
			( sem_destroy(&data->bonding_barrier) == -1)	||
			( sem_destroy(&data->wait_to_bond) == -1)		||
			( sem_destroy(&data->barrier) == -1 )			||
			( sem_destroy(&data->bonding) == -1 )			||
			( sem_destroy(&data->finish) == -1 )			||
    		( sem_destroy(&data->hydro) == -1 ) 			||
    		( sem_destroy(&data->mutex) == -1 ) 			||
    		( sem_destroy(&data->write) == -1)				||    		
    		( sem_destroy(&data->oxy) == -1)				  		
    	)  return false;
    shmdt(&data);
	shmctl(memory_id, IPC_RMID, NULL);
    return true;
}
bool parse_args( int argc, char const *argv[] )
{
	char* dirt;
	int number;
	if ( argc != OPT_COUNT )
	{
		print_arg_err();
		return false;
	}
	else if ( ( number = strtol(argv[1],&dirt,10) <= 0 ) || ( dirt[0] != '\0' ) )
	{
		print_arg_err();
		return false;
	}
	for ( int k = 2; k < OPT_COUNT ; ++k )
	{
		number = strtol(argv[k],&dirt,10);
		if ( ( number < 0 || number > MAX_TIME ) || ( dirt[0] != '\0' ) )
		{
			print_arg_err();
			return false;
		}		
	}
	data->N  = strtol(argv[1],&dirt,10);
	data->max_sleep_hydro = strtol(argv[2],&dirt,10) * K;
	data->max_sleep_oxy   = strtol(argv[3],&dirt,10) * K;
	data->max_sleep_bond  = strtol(argv[4],&dirt,10) * K;
	return true;
}
void print_arg_err()
{
	fprintf(stderr,"ERROR:\tWrong combination of arguments.\n");
	return;
}
void print_sys_err()
{
	fprintf(stderr,"ERROR:\tSystem call failed.\n");
	return;
}
void interupt_signal()
{
	clear_memory();
	exit(1);
}
/********************************************************************************************************************************/	