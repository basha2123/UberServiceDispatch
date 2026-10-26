#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include "uber_dispatch.h"

#define MEMORY_ALERT_THRESHOLD (1024U * 1024U)

#ifndef UNIT_TEST
#define UNIT_TEST 0
#endif

typedef struct MemoryBlock
{
    void *ptr;
    size_t size;
    struct MemoryBlock *next;
} MemoryBlock;

typedef struct Driver
{
    int driverId;
    int available;
} Driver;

MemoryBlock *memoryHead = NULL;
Ride *rideHead = NULL;
Driver drivers[MAX_DRIVERS];
int driverCount = 0;

size_t currentMemory = 0;
size_t peakMemory = 0;
int activeAllocations = 0;
int totalRequests = 0;
int completedRides = 0;

static pthread_mutex_t memoryMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dispatchMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
static long allocationFailureAfter = -1;
static char logFilePath[256] = "uber_dispatch.log";
static int highMemoryAlerted = 0;

static void logEvent(const char *message)
{
    FILE *logFile;
    time_t now;
    struct tm timestamp;
    char formattedTime[32];

    if (message == NULL)
    {
        return;
    }

    now = time(NULL);
    if (now == (time_t)-1 || localtime_r(&now, &timestamp) == NULL ||
        strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
                 &timestamp) == 0)
    {
        (void)fprintf(stderr, "Unable to create log timestamp.\n");
        return;
    }

    (void)pthread_mutex_lock(&logMutex);
    logFile = fopen(logFilePath, "a");
    if (logFile == NULL)
    {
        (void)pthread_mutex_unlock(&logMutex);
        (void)fprintf(stderr, "Unable to open dispatch log file.\n");
        return;
    }

    if (fprintf(logFile, "%s | %s\n", formattedTime, message) < 0)
    {
        (void)fclose(logFile);
        (void)pthread_mutex_unlock(&logMutex);
        (void)fprintf(stderr, "Unable to write dispatch log file.\n");
        return;
    }
    if (fclose(logFile) != 0)
    {
        (void)pthread_mutex_unlock(&logMutex);
        (void)fprintf(stderr, "Unable to write dispatch log file.\n");
        return;
    }
    (void)pthread_mutex_unlock(&logMutex);
}

int setLogFile(const char *path)
{
    size_t pathLength;

    if (path == NULL || path[0] == '\0')
    {
        return -1;
    }
    pathLength = strlen(path);
    if (pathLength >= sizeof(logFilePath))
    {
        return -1;
    }

    (void)pthread_mutex_lock(&logMutex);
    memcpy(logFilePath, path, pathLength + 1U);
    (void)pthread_mutex_unlock(&logMutex);
    return 0;
}

void setAllocationFailureAfter(long successfulAllocations)
{
    (void)pthread_mutex_lock(&memoryMutex);
    allocationFailureAfter = successfulAllocations;
    (void)pthread_mutex_unlock(&memoryMutex);
}

void* tracked_malloc(size_t size)
{
    void *ptr;
    MemoryBlock *node;
    int alert = 0;

    (void)pthread_mutex_lock(&memoryMutex);
    if (allocationFailureAfter == 0)
    {
        (void)pthread_mutex_unlock(&memoryMutex);
        return NULL;
    }
    if (allocationFailureAfter > 0)
    {
        allocationFailureAfter--;
    }

    ptr = malloc(size);
    if (ptr == NULL)
    {
        (void)pthread_mutex_unlock(&memoryMutex);
        return NULL;
    }

    node = (MemoryBlock *)malloc(sizeof(MemoryBlock));
    if (node == NULL)
    {
        free(ptr);
        (void)pthread_mutex_unlock(&memoryMutex);
        return NULL;
    }
    if ((size_t)-1 - currentMemory < size || activeAllocations == INT_MAX)
    {
        free(node);
        free(ptr);
        (void)pthread_mutex_unlock(&memoryMutex);
        return NULL;
    }

    node->ptr = ptr;
    node->size = size;
    node->next = memoryHead;
    memoryHead = node;

    currentMemory += size;
    if (currentMemory > peakMemory)
    {
        peakMemory = currentMemory;
    }
    if (currentMemory >= MEMORY_ALERT_THRESHOLD && highMemoryAlerted == 0)
    {
        highMemoryAlerted = 1;
        alert = 1;
    }

    activeAllocations++;
    (void)pthread_mutex_unlock(&memoryMutex);
    if (alert != 0)
    {
        logEvent("High memory usage alert");
    }
    return ptr;
}

void tracked_free(const void *ptr)
{
    MemoryBlock *curr;
    MemoryBlock *prev = NULL;

    if (ptr == NULL)
    {
        return;
    }

    (void)pthread_mutex_lock(&memoryMutex);
    curr = memoryHead;
    while (curr != NULL)
    {
        if (curr->ptr == ptr)
        {
            currentMemory -= curr->size;
            activeAllocations--;
            if (currentMemory < MEMORY_ALERT_THRESHOLD)
            {
                highMemoryAlerted = 0;
            }

            if (prev == NULL)
            {
                memoryHead = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }

            free(curr->ptr);
            free(curr);
            (void)pthread_mutex_unlock(&memoryMutex);
            return;
        }

        prev = curr;
        curr = curr->next;
    }
    (void)pthread_mutex_unlock(&memoryMutex);
}

static int assignAvailableDriverLocked(void)
{
    int i;

    for (i = 0; i < driverCount; i++)
    {
        if (drivers[i].available)
        {
            drivers[i].available = 0;
            return drivers[i].driverId;
        }
    }

    return -1;
}

int assignAvailableDriver(void)
{
    int driverId;

    (void)pthread_mutex_lock(&dispatchMutex);
    driverId = assignAvailableDriverLocked();
    (void)pthread_mutex_unlock(&dispatchMutex);
    return driverId;
}

static void releaseDriverLocked(int driverId)
{
    int i;

    for (i = 0; i < driverCount; i++)
    {
        if (drivers[i].driverId == driverId)
        {
            drivers[i].available = 1;
            return;
        }
    }
}

void releaseDriver(int driverId)
{
    (void)pthread_mutex_lock(&dispatchMutex);
    releaseDriverLocked(driverId);
    (void)pthread_mutex_unlock(&dispatchMutex);
}

void addDriver(void)
{
    int driverId;

    (void)pthread_mutex_lock(&dispatchMutex);
    if (driverCount >= MAX_DRIVERS)
    {
        (void)pthread_mutex_unlock(&dispatchMutex);
        printf("Driver limit reached.\n");
        logEvent("Driver limit reached");
        return;
    }

    drivers[driverCount].driverId = driverCount + 1;
    drivers[driverCount].available = 1;
    driverCount++;
    driverId = drivers[driverCount - 1].driverId;
    (void)pthread_mutex_unlock(&dispatchMutex);

    printf("Driver %d added successfully.\n", driverId);
    logEvent("Driver added");
}

int requestRide(const char *customerName, int *rideId, int *assignedDriverId)
{
    Ride *ride;
    int availableDriverId;
    size_t nameLength;

    if (customerName == NULL)
    {
        return -1;
    }
    nameLength = strlen(customerName);
    if (nameLength == 0U || nameLength >= MAX_CUSTOMER_NAME)
    {
        return -1;
    }

    ride = (Ride *)tracked_malloc(sizeof(Ride));
    if (ride == NULL)
    {
        logEvent("Ride allocation failed");
        return -1;
    }

    memcpy(ride->customer, customerName, nameLength + 1U);
    ride->assignedDriver = -1;
    ride->next = NULL;

    (void)pthread_mutex_lock(&dispatchMutex);
    if (totalRequests == INT_MAX)
    {
        (void)pthread_mutex_unlock(&dispatchMutex);
        tracked_free(ride);
        return -1;
    }
    availableDriverId = assignAvailableDriverLocked();
    ride->assignedDriver = availableDriverId;
    totalRequests++;
    ride->rideId = totalRequests;
    ride->next = rideHead;
    rideHead = ride;
    if (rideId != NULL)
    {
        *rideId = ride->rideId;
    }
    if (assignedDriverId != NULL)
    {
        *assignedDriverId = availableDriverId;
    }
    (void)pthread_mutex_unlock(&dispatchMutex);

    logEvent(availableDriverId == -1 ? "Ride queued without an available driver"
                                     : "Ride assigned to an available driver");
    return 0;
}

void addRideRequest(void)
{
    char customerName[MAX_CUSTOMER_NAME];
    int rideId;
    int assignedDriverId;

    printf("Enter customer name: ");
    if (scanf("%49s", customerName) != 1)
    {
        printf("Unable to read customer name.\n");
        logEvent("Ride request input failed");
        return;
    }
    if (requestRide(customerName, &rideId, &assignedDriverId) != 0)
    {
        printf("Unable to create ride request.\n");
        return;
    }
    if (assignedDriverId != -1)
    {
        printf("Ride %d assigned to Driver %d\n", rideId,
               assignedDriverId);
    }
    else
    {
        printf("No available driver. Ride queued.\n");
    }
}

void displayDrivers(void)
{
    int i;

    (void)pthread_mutex_lock(&dispatchMutex);
    printf("\nDrivers:\n");
    for (i = 0; i < driverCount; i++)
    {
        printf("Driver %d: %s\n",
               drivers[i].driverId,
               drivers[i].available ? "Available" : "Busy");
    }
    (void)pthread_mutex_unlock(&dispatchMutex);
}

void displayRideRequests(void)
{
    Ride *ride;

    (void)pthread_mutex_lock(&dispatchMutex);
    ride = rideHead;
    printf("\nActive rides:\n");
    while (ride != NULL)
    {
        printf("Ride %d | Customer: %s | Driver: %d\n",
               ride->rideId,
               ride->customer,
               ride->assignedDriver);
        ride = ride->next;
    }
    (void)pthread_mutex_unlock(&dispatchMutex);
}

void rideProcessor(const Ride *ride)
{
    if (ride == NULL)
    {
        return;
    }

    (void)pthread_mutex_lock(&dispatchMutex);
    if (ride->assignedDriver != -1)
    {
        releaseDriverLocked(ride->assignedDriver);
    }
    completedRides++;
    (void)pthread_mutex_unlock(&dispatchMutex);
    logEvent("Ride processed");
}

void processRide(void)
{
    Ride *ride;

    (void)pthread_mutex_lock(&dispatchMutex);
    ride = rideHead;
    if (ride == NULL)
    {
        (void)pthread_mutex_unlock(&dispatchMutex);
        printf("No active rides.\n");
        return;
    }

    rideHead = ride->next;
    if (ride->assignedDriver != -1)
    {
        releaseDriverLocked(ride->assignedDriver);
    }
    completedRides++;
    (void)pthread_mutex_unlock(&dispatchMutex);

    tracked_free(ride);
    logEvent("Ride processed");

    printf("Ride processed successfully.\n");
}

typedef struct SimulationWork
{
    size_t requestCount;
    size_t failureCount;
} SimulationWork;

static void *simulationWorker(void *argument)
{
    SimulationWork *work = (SimulationWork *)argument;
    size_t index;

    for (index = 0U; index < work->requestCount; index++)
    {
        if (requestRide("simulated", NULL, NULL) != 0)
        {
            work->failureCount++;
        }
    }
    return NULL;
}

int simulateRideRequests(size_t requestCount, unsigned int workerCount)
{
    pthread_t workers[MAX_SIMULATION_WORKERS];
    SimulationWork work[MAX_SIMULATION_WORKERS];
    unsigned int index;
    int result = 0;

    if (workerCount == 0U || workerCount > MAX_SIMULATION_WORKERS)
    {
        return -1;
    }
    memset(work, 0, sizeof(work));

    for (index = 0U; index < workerCount; index++)
    {
        work[index].requestCount = requestCount / workerCount;
        if ((size_t)index < requestCount % workerCount)
        {
            work[index].requestCount++;
        }
        if (pthread_create(&workers[index], NULL, simulationWorker,
                           &work[index]) != 0)
        {
            result = -1;
            workerCount = index;
            break;
        }
    }
    for (index = 0U; index < workerCount; index++)
    {
        if (pthread_join(workers[index], NULL) != 0)
        {
            result = -1;
        }
        if (work[index].failureCount != 0U)
        {
            result = -1;
        }
    }
    return result;
}

void getMemoryStatistics(MemoryStatistics *statistics)
{
    if (statistics == NULL)
    {
        return;
    }
    (void)pthread_mutex_lock(&memoryMutex);
    statistics->currentBytes = currentMemory;
    statistics->peakBytes = peakMemory;
    statistics->allocationCount = activeAllocations;
    (void)pthread_mutex_unlock(&memoryMutex);

    (void)pthread_mutex_lock(&dispatchMutex);
    statistics->requestCount = totalRequests;
    statistics->completedCount = completedRides;
    (void)pthread_mutex_unlock(&dispatchMutex);
}

void memoryStatistics(void)
{
    MemoryStatistics statistics;

    getMemoryStatistics(&statistics);
    printf("\nMemory Statistics:\n");
    printf("Current Memory: %zu bytes\n", statistics.currentBytes);
    printf("Peak Memory: %zu bytes\n", statistics.peakBytes);
    printf("Active Allocations: %d\n", statistics.allocationCount);
}

void leakReport(void)
{
    MemoryBlock *curr;
    size_t leaked = 0;

    (void)pthread_mutex_lock(&memoryMutex);
    curr = memoryHead;
    printf("\nLeak Report:\n");
    while (curr != NULL)
    {
        leaked += curr->size;
        printf("Leaked block: %zu bytes\n", curr->size);
        curr = curr->next;
    }

    printf("Total leaked memory: %zu bytes\n", leaked);
    (void)pthread_mutex_unlock(&memoryMutex);
}

void cleanup(void)
{
    Ride *rides;

    (void)pthread_mutex_lock(&dispatchMutex);
    rides = rideHead;
    rideHead = NULL;
    (void)pthread_mutex_unlock(&dispatchMutex);

    while (rides != NULL)
    {
        Ride *next = rides->next;
        rides->next = NULL;
        tracked_free(rides);
        rides = next;
    }

    (void)pthread_mutex_lock(&memoryMutex);
    while (memoryHead != NULL)
    {
        MemoryBlock *next = memoryHead->next;
        free(memoryHead->ptr);
        free(memoryHead);
        memoryHead = next;
    }

    currentMemory = 0;
    peakMemory = 0;
    activeAllocations = 0;
    allocationFailureAfter = -1;
    highMemoryAlerted = 0;
    (void)pthread_mutex_unlock(&memoryMutex);

    (void)pthread_mutex_lock(&dispatchMutex);
    totalRequests = 0;
    completedRides = 0;

    for (int i = 0; i < MAX_DRIVERS; i++)
    {
        drivers[i].driverId = 0;
        drivers[i].available = 0;
    }

    driverCount = 0;
    (void)pthread_mutex_unlock(&dispatchMutex);
}

#if !UNIT_TEST

int main(void)
{
    int choice;

    while (1)
    {
        printf("\n");
        printf("====================================\n");
        printf(" UBER DISPATCH MEMORY ANALYZER\n");
        printf("====================================\n");

        printf("1. Add Driver\n");
        printf("2. Create Ride Request\n");
        printf("3. Display Drivers\n");
        printf("4. Display Active Rides\n");
        printf("5. Process Ride\n");
        printf("6. Memory Statistics\n");
        printf("7. Leak Report\n");
        printf("8. Exit\n");

        printf("Enter Choice : ");
        if (scanf("%d", &choice) != 1)
        {
            int inputCharacter;

            if (feof(stdin))
            {
                cleanup();
                return 0;
            }
            while ((inputCharacter = getchar()) != '\n' &&
                   inputCharacter != EOF)
            {
            }
            printf("Invalid Choice\n");
            continue;
        }

        switch (choice)
        {
            case 1:
                addDriver();
                break;

            case 2:
                addRideRequest();
                break;

            case 3:
                displayDrivers();
                break;

            case 4:
                displayRideRequests();
                break;

            case 5:
                processRide();
                break;

            case 6:
                memoryStatistics();
                break;

            case 7:
                leakReport();
                break;

            case 8:
                cleanup();
                printf("\nFinal Leak Check\n");
                leakReport();
                printf("\nExiting...\n");
                return 0;

            default:
                printf("Invalid Choice\n");
        }
    }

    return 0;
}

#endif
