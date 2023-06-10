#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define VEHICLE_AMOUNT_FOR_EACH_TYPE 8
#define WAITING_QUEUE_REMAINING_CAPACITY_YENIHISAR 30
#define WAITING_QUEUE_REMAINING_CAPACITY_TOPCULAR 30

typedef enum
{
    TRUCK,
    BUS,
    CAR,
    MOTORCYCLE
} VehicleType;

// Struct for vehicle
typedef struct
{
    VehicleType type;
    int id;               // unique id for each vehicle
    int size;             // 4 for truck, 3 for bus, 2 for car, 1 for motorcycle
    int is_special_group; // 1 for is_special group, 0 for regular
    int start_port;       // 0 for Eskihisar, 1 for Topcular
    int line_id;          // 0 for Line1, 1 for Line2, 2 for Line3
    char *status;         // "waiting", "boarding", "on_ferry", "off_ferry", "arrived"
} Vehicle;

// Node for queue
typedef struct Node
{
    Vehicle *vehicle;
    struct Node *next;
} Node;
// Struct for ferry
typedef struct
{
    int id;
    int capacity; // 30 units
} Ferry;

// Queue data structure
typedef struct Queue
{
    Node *front;
    Node *rear;
    int size;
    int capacity;
} Queue;
// Struct for port
typedef struct
{
    int id;                    // 0 for Eskihisar, 1 for Topcular
    int ferry_id;              // id of the ferry at the port
    int current_line;          // 0 for Line1, 1 for Line2, 2 for Line3
    Queue waiting_queue[3];    // 3 waiting queues
    Queue gettingBackqueue[3]; // 3 getting back queues
} Port;

// Create and initialize for each vehicle type separately
Vehicle trucks[VEHICLE_AMOUNT_FOR_EACH_TYPE];
Vehicle buses[VEHICLE_AMOUNT_FOR_EACH_TYPE];
Vehicle cars[VEHICLE_AMOUNT_FOR_EACH_TYPE];
Vehicle motorcycles[VEHICLE_AMOUNT_FOR_EACH_TYPE];

// Create and initialize the booths of Eskihisar port
int boothEskihisar[4];

// Create and initialize the booths of Topcular port
int boothTopcular[4];

// Create and initialize the first ferry struct
Ferry ferry1;

// Create and initialize the second ferry struct
Ferry ferry2;

// Create and initialize the Eskişehir port
Port portEskihisar;

// Create and initialize the Topcular port
Port portTopcular;

// these are for providing only one vehicle can enter or leave the ferry at a time
sem_t sem_ferry1;
sem_t sem_ferry2;

// these are for providing only one queue can be accessed at a time in a specific port
sem_t sem_portEskihisar_waiting_queue[3];
sem_t sem_portTopcular_waiting_queue[3];

// these are for deciding which booth will be used by the vehicle
sem_t sem_boothEskihisar[4];
sem_t sem_boothTopcular[4];

// Initialize a queue
void init_queue(Queue *q, int max_capacity)
{
    q->front = q->rear = NULL;
    q->size = 0;
    q->capacity = max_capacity;
}

// Add an element to the end of the queue
void enqueue(Queue *q, Vehicle *v)
{
    Node *new_node = malloc(sizeof(Node));
    new_node->vehicle = v;
    new_node->next = NULL;

    if (q->rear == NULL)
    {
        q->front = q->rear = new_node;
    }
    else
    {
        q->rear->next = new_node;
        q->rear = new_node;
    }

    q->size++;
}

// Remove an element from the front of the queue
Vehicle *dequeue(Queue *q)
{
    if (q->front == NULL)
        return NULL; // Queue is empty

    Node *temp = q->front;

    Vehicle *v = temp->vehicle;
    q->front = q->front->next;

    if (q->front == NULL)
    {
        q->rear = NULL;
    }

    free(temp);
    q->size--;

    return v;
}

// Get the size of the queue
int get_queue_size(Queue *q)
{
    return q->size;
}

void *vehicle_thread(void *arg)
{
    Vehicle *vehicle = (Vehicle *)arg;

    char *vehicleType;

    switch (vehicle->type)
    {
    case TRUCK:
        vehicleType = "TRUCK";
        break;
    case BUS:
        vehicleType = "BUS";
        break;
    case CAR:
        vehicleType = "CAR";
        break;
    case MOTORCYCLE:
        vehicleType = "MOTORCYCLE";
        break;
    default:
        vehicleType = "UNKNOWN";
    }

    // Decide which port this vehicle belongs to
    int *booths;
    sem_t *sem_booths; // <-- changed to sem_t *

    Port *port;
    sem_t *sem_queues;

    if (vehicle->start_port == 0)
    {
        booths = boothEskihisar;
        sem_booths = sem_boothEskihisar;
        port = &portEskihisar;
        sem_queues = sem_portEskihisar_waiting_queue;
    }
    else
    {
        booths = boothTopcular;
        sem_booths = sem_boothTopcular;
        port = &portTopcular;
        sem_queues = sem_portTopcular_waiting_queue;
    }

    // Vehicle chooses a booth randomly
    int booth_id = vehicle->is_special_group ? rand() % 4 : rand() % 3;
    // printf("%s%d starting at port %d has selected booth %d because the speciality is %d\n" vehicleType, vehicle->id, vehicle->start_port, booth_id, vehicle->is_special_group);
    sem_wait(&sem_booths[booth_id]);
    booths[booth_id]++; // Vehicle enters the booth

    // Simulation of the payment transaction
    // sleep(rand() % 3 + 1); // Sleeps between 1 to 3 seconds

    booths[booth_id]--; // Vehicle leaves the booth

    sem_post(&sem_booths[booth_id]);

    // Add vehicle to a waiting line
    for (int i = 0; i < 3; i++)
    {
        int line_id = (i + port->current_line) % 3;

        sem_wait(&sem_queues[line_id]);
        // if waiting line is not full, then add vehicle to the current waiting line
        if (vehicle->size <= port->waiting_queue[line_id].capacity)
        {
            enqueue(&port->waiting_queue[line_id], vehicle);
            port->waiting_queue[line_id].capacity -= vehicle->size;
            vehicle->status = "waiting";

            printf("%s%d starting at port %d has entered the waiting queue %d and the order is %d\n",
                   vehicleType, vehicle->id, vehicle->start_port, line_id, get_queue_size(&port->waiting_queue[line_id]));

            sem_post(&sem_queues[line_id]);
            break;
        }

        sem_post(&sem_queues[line_id]);
    }

    return NULL;
}

void *ferry_thread(void *arg)
{
    Ferry *ferry = (Ferry *)arg;
    Port *port;
    sem_t *sem_queues;

    if (ferry->id == portEskihisar.ferry_id)
    {
        port = &portEskihisar;
        sem_queues = sem_portEskihisar_waiting_queue;
    }
    else
    {
        port = &portTopcular;
        sem_queues = sem_portTopcular_waiting_queue;
    }

    while (1)
    {
        // close the other two lines and open the current line
        for (int i = 0; i < 3; i++)
        {
            if (i != port->current_line)
                sem_wait(&sem_queues[i]);
        }

        while (get_queue_size(&port->waiting_queue[port->current_line]) > 0 && ferry->capacity > 0)
        {
            sem_wait(&sem_queues[port->current_line]);
            Vehicle *vehicle = dequeue(&port->waiting_queue[port->current_line]);
            // enqueue(&port->gettingBackqueue[port->current_line], vehicle);
            if (vehicle != NULL && ferry->capacity >= vehicle->size)
            {
                ferry->capacity -= vehicle->size;
                vehicle->status = "on_ferry";
                port->waiting_queue[port->current_line].capacity += vehicle->size; // Increase queue capacity as vehicle is leaving
                printf("Vehicle %d of type %d has boarded on ferry %d. Remaining capacity of ferry: %d\n",
                       vehicle->id, vehicle->type, ferry->id, ferry->capacity);
            }
            else
            {
                enqueue(&port->waiting_queue[port->current_line], vehicle);
            }
            sem_post(&sem_queues[port->current_line]);
        }

        // After a line has been processed, the current line points to the next line
        port->current_line = (port->current_line + 1) % 3;

        // open the other two lines and close the current line
        for (int i = 0; i < 3; i++)
        {
            if (i != port->current_line)
                sem_post(&sem_queues[i]);
        }
    }

    return NULL;
}

int main()
{ // Initialize the waiting queues
    for (int i = 0; i < 3; i++)
    {
        init_queue(&portEskihisar.waiting_queue[i], 20);
        init_queue(&portTopcular.waiting_queue[i], 20);
    }

    for (int i = 0; i < 4; i++)
    {
        sem_init(&sem_boothEskihisar[i], 0, 1);
        sem_init(&sem_boothTopcular[i], 0, 1);
        if (i < 3)
        {
            sem_init(&sem_portEskihisar_waiting_queue[i], 0, 1);
            sem_init(&sem_portTopcular_waiting_queue[i], 0, 1);
        }
    }

    // creating 2 pipes for each port to send and receive data
    int fd1[2];
    int fd2[2];

    pipe(fd1); // from eskisehir to topcular
    pipe(fd2); // from topcular to eskisehir

    srand(time(NULL)); // Initialize random seed
    int truck_id = 0;
    int bus_id = 0;
    int car_id = 0;
    int motorcycle_id = 0;

    for (int i = 0; i < VEHICLE_AMOUNT_FOR_EACH_TYPE; i++)
    {
        trucks[i].type = TRUCK;
        trucks[i].id = truck_id++;
        trucks[i].size = 4;
        trucks[i].is_special_group = rand() % 2;
        trucks[i].start_port = rand() % 2;

        buses[i].type = BUS;
        buses[i].id = bus_id++;
        buses[i].size = 3;
        buses[i].is_special_group = rand() % 2;
        buses[i].start_port = rand() % 2;

        cars[i].type = CAR;
        cars[i].id = car_id++;
        cars[i].size = 2;
        cars[i].is_special_group = rand() % 2;
        cars[i].start_port = rand() % 2;

        motorcycles[i].type = MOTORCYCLE;
        motorcycles[i].id = motorcycle_id++;
        motorcycles[i].size = 1;
        motorcycles[i].is_special_group = rand() % 2;
        motorcycles[i].start_port = rand() % 2;
    }

    ferry1.id = 0;
    ferry1.capacity = 30;

    ferry2.id = 1;
    ferry2.capacity = 30;

    portEskihisar.id = 0;
    portEskihisar.ferry_id = 0;
    portEskihisar.current_line = 0;

    portTopcular.id = 1;
    portTopcular.ferry_id = 1;
    portTopcular.current_line = 0;

    // writing to pipes about ports
    write(fd1[1], &portEskihisar, sizeof(portEskihisar));
    write(fd2[1], &portTopcular, sizeof(portTopcular));

    // writing to pipes about ferries
    // write(fd1[1], &ferry1, sizeof(ferry1));
    write(fd2[1], &ferry2, sizeof(ferry2));

    // reading from pipes
    read(fd1[0], &portEskihisar, sizeof(portEskihisar));
    read(fd2[0], &portTopcular, sizeof(portTopcular));

    pthread_t threads[VEHICLE_AMOUNT_FOR_EACH_TYPE * 4 + 2]; // additional 2 threads for the ferries

    // create vehicle threads
    for (int i = 0; i < VEHICLE_AMOUNT_FOR_EACH_TYPE; i++)
    {
        pthread_create(&threads[i * 4], NULL, vehicle_thread, &trucks[i]);
        pthread_create(&threads[i * 4 + 1], NULL, vehicle_thread, &buses[i]);
        pthread_create(&threads[i * 4 + 2], NULL, vehicle_thread, &cars[i]);
        pthread_create(&threads[i * 4 + 3], NULL, vehicle_thread, &motorcycles[i]);
    }

    // create ferry threads
    pthread_create(&threads[VEHICLE_AMOUNT_FOR_EACH_TYPE * 4], NULL, ferry_thread, &ferry1);
    pthread_create(&threads[VEHICLE_AMOUNT_FOR_EACH_TYPE * 4 + 1], NULL, ferry_thread, &ferry2);

    // join vehicle threads
    for (int i = 0; i < VEHICLE_AMOUNT_FOR_EACH_TYPE * 4 + 2; i++)
    {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
