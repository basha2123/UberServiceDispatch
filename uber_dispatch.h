#ifndef UBER_DISPATCH_H
#define UBER_DISPATCH_H

#include <stddef.h>

#define MAX_DRIVERS 10
#define MAX_CUSTOMER_NAME 50
#define MAX_SIMULATION_WORKERS 64U

typedef struct Ride
{
    int rideId;
    char customer[MAX_CUSTOMER_NAME];
    int assignedDriver;
    struct Ride *next;
} Ride;

typedef struct MemoryStatistics
{
    size_t currentBytes;
    size_t peakBytes;
    int allocationCount;
    int requestCount;
    int completedCount;
} MemoryStatistics;

int setLogFile(const char *path);
void setAllocationFailureAfter(long successfulAllocations);
void *tracked_malloc(size_t size);
void tracked_free(const void *ptr);
int assignAvailableDriver(void);
void releaseDriver(int driverId);
void addDriver(void);
int requestRide(const char *customerName, int *rideId, int *assignedDriverId);
void addRideRequest(void);
void displayDrivers(void);
void displayRideRequests(void);
void rideProcessor(const Ride *ride);
void processRide(void);
int simulateRideRequests(size_t requestCount, unsigned int workerCount);
void getMemoryStatistics(MemoryStatistics *statistics);
void memoryStatistics(void);
void leakReport(void);
void cleanup(void);

#endif
