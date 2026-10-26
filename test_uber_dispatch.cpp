#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

extern "C"
{
#include "uber_dispatch.h"
}

namespace
{
const char *const testLogPath = "reports/test_dispatch.log";
}

class UberDispatchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        cleanup();
        ASSERT_EQ(setLogFile(testLogPath), 0);
    }

    void TearDown() override
    {
        cleanup();
        std::remove(testLogPath);
    }
};

TEST_F(UberDispatchTest, ValidatesLogFilePathAndWritesTimestampedEvents)
{
    char longPath[257];
    std::memset(longPath, 'x', sizeof(longPath));
    longPath[sizeof(longPath) - 1U] = '\0';

    EXPECT_EQ(setLogFile(nullptr), -1);
    EXPECT_EQ(setLogFile(""), -1);
    EXPECT_EQ(setLogFile(longPath), -1);
    addDriver();

    std::ifstream logFile(testLogPath);
    ASSERT_TRUE(logFile.good());
    const std::string contents((std::istreambuf_iterator<char>(logFile)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find(" | Driver added"), std::string::npos);
    EXPECT_NE(contents.find("20"), std::string::npos);
}

TEST_F(UberDispatchTest, AddsDriversWithinCapacityAndReleasesAssignments)
{
    for (int index = 0; index < MAX_DRIVERS; ++index)
    {
        addDriver();
    }

    for (int index = 1; index <= MAX_DRIVERS; ++index)
    {
        EXPECT_EQ(assignAvailableDriver(), index);
    }
    EXPECT_EQ(assignAvailableDriver(), -1);

    releaseDriver(1);
    EXPECT_EQ(assignAvailableDriver(), 1);
    releaseDriver(1);

    addDriver();
    EXPECT_EQ(assignAvailableDriver(), 1);
    EXPECT_EQ(assignAvailableDriver(), -1);
}

TEST_F(UberDispatchTest, TracksAllocationsFailureInjectionAndUnknownFrees)
{
    MemoryStatistics statistics{};
    int unrelatedValue = 0;

    setAllocationFailureAfter(0L);
    EXPECT_EQ(tracked_malloc(32U), nullptr);

    setAllocationFailureAfter(1L);
    void *firstAllocation = tracked_malloc(32U);
    ASSERT_NE(firstAllocation, nullptr);
    EXPECT_EQ(tracked_malloc(8U), nullptr);
    tracked_free(nullptr);
    tracked_free(&unrelatedValue);
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.currentBytes, 32U);
    EXPECT_EQ(statistics.allocationCount, 1);

    tracked_free(firstAllocation);
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.allocationCount, 0);

    setAllocationFailureAfter(-1L);
    void *olderAllocation = tracked_malloc(64U);
    void *newerAllocation = tracked_malloc(96U);
    ASSERT_NE(olderAllocation, nullptr);
    ASSERT_NE(newerAllocation, nullptr);
    tracked_free(olderAllocation);
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.currentBytes, 96U);
    EXPECT_EQ(statistics.allocationCount, 1);
    tracked_free(newerAllocation);
}

TEST_F(UberDispatchTest, ReportsLogFileOpenFailure)
{
    ASSERT_EQ(setLogFile("/missing-parent-for-uber-dispatch/test.log"), 0);

    testing::internal::CaptureStderr();
    addDriver();
    const std::string error = testing::internal::GetCapturedStderr();
    EXPECT_NE(error.find("Unable to open dispatch log file"),
              std::string::npos);

    EXPECT_EQ(setLogFile(testLogPath), 0);
}

TEST_F(UberDispatchTest, LogsHighMemoryAlertsAndResetsTheAlertAfterFree)
{
    void *firstAllocation = tracked_malloc(1024U * 1024U);
    ASSERT_NE(firstAllocation, nullptr);
    tracked_free(firstAllocation);

    void *secondAllocation = tracked_malloc(1024U * 1024U);
    ASSERT_NE(secondAllocation, nullptr);
    tracked_free(secondAllocation);

    std::ifstream logFile(testLogPath);
    const std::string contents((std::istreambuf_iterator<char>(logFile)),
                               std::istreambuf_iterator<char>());
    const std::string alert = "High memory usage alert";
    const size_t firstAlert = contents.find(alert);
    ASSERT_NE(firstAlert, std::string::npos);
    EXPECT_NE(contents.find(alert, firstAlert + alert.size()),
              std::string::npos);
}

TEST_F(UberDispatchTest, RejectsInvalidAndFailedRideRequests)
{
    int rideId = 0;
    int driverId = 0;
    char longName[MAX_CUSTOMER_NAME + 1];
    std::memset(longName, 'a', MAX_CUSTOMER_NAME);
    longName[MAX_CUSTOMER_NAME] = '\0';

    EXPECT_EQ(requestRide(nullptr, &rideId, &driverId), -1);
    EXPECT_EQ(requestRide("", &rideId, &driverId), -1);
    EXPECT_EQ(requestRide(longName, &rideId, &driverId), -1);

    setAllocationFailureAfter(0L);
    EXPECT_EQ(requestRide("Rider", &rideId, &driverId), -1);
    setAllocationFailureAfter(-1L);

    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.requestCount, 0);
    EXPECT_EQ(statistics.allocationCount, 0);
}

TEST_F(UberDispatchTest, AssignsAndProcessesRideAndUpdatesStatistics)
{
    addDriver();
    int rideId = 0;
    int driverId = 0;
    ASSERT_EQ(requestRide("Rider", &rideId, &driverId), 0);
    EXPECT_EQ(rideId, 1);
    EXPECT_EQ(driverId, 1);

    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.requestCount, 1);
    EXPECT_EQ(statistics.completedCount, 0);
    EXPECT_EQ(statistics.allocationCount, 1);
    EXPECT_GT(statistics.peakBytes, 0U);

    processRide();
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.completedCount, 1);
    EXPECT_EQ(statistics.allocationCount, 0);
    EXPECT_EQ(assignAvailableDriver(), 1);
}

TEST_F(UberDispatchTest, QueuesRidesWhenThereAreNoAvailableDrivers)
{
    int rideId = 0;
    int driverId = 0;

    ASSERT_EQ(requestRide("Queued", &rideId, &driverId), 0);
    EXPECT_EQ(driverId, -1);
    EXPECT_EQ(rideId, 1);

    processRide();
    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.completedCount, 1);
    EXPECT_EQ(statistics.allocationCount, 0);
}

TEST_F(UberDispatchTest, HandlesMissingRidesAndNullStatistics)
{
    processRide();
    getMemoryStatistics(nullptr);
    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.requestCount, 0);
    EXPECT_EQ(statistics.completedCount, 0);
}

TEST_F(UberDispatchTest, ExercisesDisplayAndStatisticsFunctions)
{
    addDriver();
    ASSERT_EQ(requestRide("Visible", nullptr, nullptr), 0);

    testing::internal::CaptureStdout();
    displayDrivers();
    displayRideRequests();
    memoryStatistics();
    leakReport();
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("Driver 1"), std::string::npos);
    EXPECT_NE(output.find("Visible"), std::string::npos);
    EXPECT_NE(output.find("Memory Statistics"), std::string::npos);
    EXPECT_NE(output.find("Leaked block"), std::string::npos);

    cleanup();
    testing::internal::CaptureStdout();
    leakReport();
    const std::string cleanOutput = testing::internal::GetCapturedStdout();
    EXPECT_NE(cleanOutput.find("Total leaked memory: 0 bytes"),
              std::string::npos);
}

TEST_F(UberDispatchTest, ProcessesStandaloneRideAndReleasesItsDriver)
{
    addDriver();
    const int driverId = assignAvailableDriver();
    ASSERT_EQ(driverId, 1);

    Ride ride{};
    ride.rideId = 77;
    ride.assignedDriver = driverId;
    std::strcpy(ride.customer, "Standalone");
    rideProcessor(&ride);

    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.completedCount, 1);
    EXPECT_EQ(assignAvailableDriver(), driverId);

    rideProcessor(nullptr);
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.completedCount, 1);
}

TEST_F(UberDispatchTest, SimulatesConcurrentRideRequestsAndRejectsInvalidWorkerCounts)
{
    EXPECT_EQ(simulateRideRequests(1U, 0U), -1);
    EXPECT_EQ(simulateRideRequests(1U, MAX_SIMULATION_WORKERS + 1U), -1);
    EXPECT_EQ(simulateRideRequests(0U, 1U), 0);
    ASSERT_EQ(simulateRideRequests(80U, 8U), 0);

    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.requestCount, 80);
    EXPECT_EQ(statistics.allocationCount, 80);

    testing::internal::CaptureStdout();
    for (int index = 0; index < 80; ++index)
    {
        processRide();
    }
    static_cast<void>(testing::internal::GetCapturedStdout());
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.completedCount, 80);
    EXPECT_EQ(statistics.allocationCount, 0);
}

TEST_F(UberDispatchTest, CleanupReleasesAllOutstandingRideAllocations)
{
    ASSERT_EQ(requestRide("First", nullptr, nullptr), 0);
    ASSERT_EQ(requestRide("Second", nullptr, nullptr), 0);
    cleanup();

    MemoryStatistics statistics{};
    getMemoryStatistics(&statistics);
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.peakBytes, 0U);
    EXPECT_EQ(statistics.allocationCount, 0);
    EXPECT_EQ(statistics.requestCount, 0);
    EXPECT_EQ(statistics.completedCount, 0);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
