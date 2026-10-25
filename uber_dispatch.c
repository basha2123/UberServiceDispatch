#ifndef UNIT_TEST

int main()
{
    int choice;

    while(1)
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
        scanf("%d", &choice);

        switch(choice)
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
