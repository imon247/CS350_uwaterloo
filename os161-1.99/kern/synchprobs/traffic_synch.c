#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#define MAX_THREADS 10

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */



static struct cv *pass;
static struct lock *lock;

typedef struct Vehicles{
	Direction from;
	Direction to;
}Vehicle, *VehiclePtr;

static Vehicle * volatile vehicles[MAX_THREADS];


volatile int threads_in_traffic;				//
bool right_turn(Vehicle *current);	//
VehiclePtr makeVehicle(Direction from, Direction to);		//
void enter_traffic(Vehicle *current);		//
bool check_constraints(Vehicle *current);
/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */

/*
void
intersection_sync_init(void)
{
  //intersectionSem = sem_create("intersectionSem",1);
  lock = lock_create("intersectionlock");
  //if (intersectionSem == NULL) {
    //panic("could not create intersection semaphore");
  //}
  if(lock==NULL) panic("could not create lock");
  return;
}
*/

VehiclePtr makeVehicle(Direction from, Direction to){
	VehiclePtr temp;
	temp = (VehiclePtr)kmalloc(sizeof(Vehicle));
	temp->from = from;
	temp->to = to;
	return temp;
}

void enter_traffic(Vehicle *current){
	vehicles[threads_in_traffic] = current;
	threads_in_traffic++;
}

void intersection_sync_init(void){
	lock = lock_create("lock for condition");
	pass = cv_create("pass conditinal variable");
	
	if(lock==NULL){
		panic("could not create lock");
	}
	threads_in_traffic = 0;
	return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */


/*
void
intersection_sync_cleanup(void)
{
  //KASSERT(intersectionSem != NULL);
  //sem_destroy(intersectionSem);
  KASSERT(lock!=NULL);
  lock_destroy(lock);
}
*/

void intersection_sync_cleanup(void){
	KASSERT(lock!=NULL); 
	cv_destroy(pass);
	lock_destroy(lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void intersection_before_entry(Direction origin, Direction destination){
	VehiclePtr current = makeVehicle(origin, destination);
	bool permissive;
	lock_acquire(lock);
	permissive = check_constraints(current);
	if(permissive == true) enter_traffic(current);
	else{
		while(permissive == false){ 
			cv_wait(pass, lock);
			permissive = check_constraints(current);
		}
		enter_traffic(current);
	}
	cv_broadcast(pass, lock);
	lock_release(lock);
}

bool right_turn(Vehicle *current){
	KASSERT(current!=NULL);
	if(current->from - current->to == 1 || current->to-current->from==3) return true;
	return false;
}

bool check_constraints(Vehicle *current){
	int i;
	//KASSERT(threads_in_traffic < MAX_THREADS);
	//bool c1, c2, c3;
	for(i=0;i<threads_in_traffic;i++){
		if(current->from == vehicles[i]->from) continue;
		if(current->from == vehicles[i]->to && current->to == vehicles[i]->from) continue;
		if((right_turn(vehicles[i]) || right_turn(current)) && (current->to!=vehicles[i]->to)) continue;
		return false;
	}
	return true;
}



/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */


void intersection_after_exit(Direction origin, Direction destination){
	lock_acquire(lock);
	int i=0;
	while(i<threads_in_traffic){
		if(vehicles[i]->from==origin && vehicles[i]->to==destination){
			VehiclePtr temp = vehicles[i];
			vehicles[i]=NULL;
			kfree(temp);
			while(i<threads_in_traffic-1){
				vehicles[i] = vehicles[i+1];
				i++;		
			}	
		}
		i++;

	}
	threads_in_traffic --;

	cv_broadcast(pass, lock);
	lock_release(lock);
}











