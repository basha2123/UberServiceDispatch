#define UNIT_TEST

#include <gtest/gtest.h>

extern "C"
{
#include "uber_dispatch.c"
}

class UberDispatchTest : public ::testing::Test
{
protected:

    void SetUp() override
    {
        rideHead = NULL;
        memoryHead = NULL;

        driverCount = 0;

        currentMemory = 0;
        peakMemory = 0;
        activeAllocations = 0;

        totalRequests = 0;
        completedRides = 0;
    }

    void TearDown() override
    {
        cleanup();
    }
};

/* =====================================================
   MEMORY TESTS
   ===================================================== */

TEST_F(UberDispatchTest, TrackedMallocSuccess)
{
    void* ptr = tracked_malloc(100);

    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(currentMemory, 100);
    EXPECT_EQ(activeAllocations, 1);

    tracked_free(ptr);
}

TEST_F(UberDispatchTest, TrackedFreeNullPointer)
{
    tracked_free(NULL);

    EXPECT_EQ(currentMemory, 0);
    EXPECT_EQ(activeAllocations, 0);
}

TEST_F(UberDispatchTest, TrackedFreeValidPointer)
{
    void* ptr = tracked_malloc(50);

    tracked_free(ptr);

    EXPECT_EQ(currentMemory, 0);
    EXPECT_EQ(activeAllocations, 0);
}

TEST_F(UberDispatchTest, PeakMemoryUpdated)
{
    void* p1 = tracked_malloc(100);
    void* p2 = tracked_malloc(200);

    EXPECT_EQ(peakMemory, 300);

    tracked_free(p1);
    tracked_free(p2);
}

/* =====================================================
   DRIVER TESTS
   ===================================================== */

TEST_F(UberDispatchTest, AssignAvailableDriver)
{
    drivers[0].driverId = 101;
    drivers[0].available = 1;

    driverCount = 1;

    int id = assignAvailableDriver();

    EXPECT_EQ(id, 101);
    EXPECT_EQ(drivers[0].available, 0);
}

TEST_F(UberDispatchTest, AssignNoAvailableDriver)
{
    drivers[0].driverId = 101;
    drivers[0].available = 0;

    driverCount = 1;

    EXPECT_EQ(assignAvailableDriver(), -1);
}

TEST_F(UberDispatchTest, ReleaseDriverSuccess)
{
    drivers[0].driverId = 101;
    drivers[0].available = 0;

    driverCount = 1;

    releaseDriver(101);

    EXPECT_EQ(drivers[0].available, 1);
}

TEST_F(UberDispatchTest, ReleaseDriverNotFound)
{
    drivers[0].driverId = 101;
    drivers[0].available = 0;

    driverCount = 1;

    releaseDriver(999);

    EXPECT_EQ(drivers[0].available, 0);
}

/* =====================================================
   RIDE TESTS
   ===================================================== */

TEST_F(UberDispatchTest, CreateRideNode)
{
    Ride* ride =
        (Ride*)tracked_malloc(sizeof(Ride));

    ASSERT_NE(ride, nullptr);

    ride->rideId = 1;
    ride->assignedDriver = 101;
    ride->next = NULL;

    rideHead = ride;

    EXPECT_EQ(rideHead->rideId, 1);
}

TEST_F(UberDispatchTest, LinkedListMultipleRides)
{
    Ride* r1 =
        (Ride*)tracked_malloc(sizeof(Ride));

    Ride* r2 =
        (Ride*)tracked_malloc(sizeof(Ride));

    r1->rideId = 1;
    r2->rideId = 2;

    r1->next = r2;
    r2->next = NULL;

    rideHead = r1;

    EXPECT_EQ(rideHead->rideId, 1);
    EXPECT_EQ(rideHead->next->rideId, 2);
}

/* =====================================================
   THREAD TEST
   ===================================================== */

TEST_F(UberDispatchTest, RideProcessorExecution)
{
    drivers[0].driverId = 101;
    drivers[0].available = 0;

    driverCount = 1;

    Ride ride;

    ride.rideId = 1;

    strcpy(ride.customer, "John");

    ride.assignedDriver = 101;

    completedRides = 0;

    rideProcessor(&ride);

    EXPECT_EQ(completedRides, 1);
    EXPECT_EQ(drivers[0].available, 1);
}

/* =====================================================
   LEAK TESTS
   ===================================================== */

TEST_F(UberDispatchTest, LeakExists)
{
    tracked_malloc(200);

    EXPECT_NE(memoryHead, nullptr);
}

TEST_F(UberDispatchTest, NoLeakAfterFree)
{
    void* ptr = tracked_malloc(200);

    tracked_free(ptr);

    EXPECT_EQ(memoryHead, nullptr);
}

/* =====================================================
   CLEANUP TESTS
   ===================================================== */

TEST_F(UberDispatchTest, CleanupSingleRide)
{
    Ride* ride =
        (Ride*)tracked_malloc(sizeof(Ride));

    ride->next = NULL;

    rideHead = ride;

    cleanup();

    rideHead = NULL;

    EXPECT_EQ(currentMemory, 0);
}

TEST_F(UberDispatchTest, CleanupMultipleRides)
{
    Ride* r1 =
        (Ride*)tracked_malloc(sizeof(Ride));

    Ride* r2 =
        (Ride*)tracked_malloc(sizeof(Ride));

    Ride* r3 =
        (Ride*)tracked_malloc(sizeof(Ride));

    r1->next = r2;
    r2->next = r3;
    r3->next = NULL;

    rideHead = r1;

    cleanup();

    rideHead = NULL;

    EXPECT_EQ(currentMemory, 0);
}

/* =====================================================
   COUNTER TESTS
   ===================================================== */

TEST_F(UberDispatchTest, RequestCounter)
{
    totalRequests++;

    EXPECT_EQ(totalRequests, 1);
}

TEST_F(UberDispatchTest, CompletedRideCounter)
{
    completedRides++;

    EXPECT_EQ(completedRides, 1);
}

/* =====================================================
   MAIN
   ===================================================== */

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
