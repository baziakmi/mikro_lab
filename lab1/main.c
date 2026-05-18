#include <stdio.h>
#include <stdlib.h>

// assembly functions
extern int access_hash(char *str); 
extern int clearance_lvl(int hash);
extern int lucas_sequence(int n);
extern int str_checksum(char *str);

void run_test_case(char *test_string) {
    printf("--- Testing PIN: \"%s\" ---\n", test_string);

    // 1. Calculate Hash
    int hash = access_hash(test_string);
    printf("Access Hash:      %d\n", hash);

    // 2. Calculate Clearance Level (Set bits % 6)
    int level = clearance_lvl(hash);
    printf("Clearance Level:  %d\n", level);

    // 3. Calculate Lucas Number (L_level)
    int lucas = lucas_sequence(level);
    printf("Lucas Code:       %d\n", lucas);

    // 4. Calculate XOR Checksum
    int checksum = str_checksum(test_string);
    printf("Checksum (XOR):   0x%02X\n", (unsigned int)checksum);
    
    printf("---------------------------\n\n");
}

int main() {
    // Array of strings to check every logic branch in your assembly
    char *test_cases[] = {
        "9",    // Number case (PrimeTable[9] = 29)
        "A",    // Uppercase case (65 * 2 = 130)
        "a",    // Lowercase case (97 AND 0xDF = 65)
        "A9b3", // Mixed case (The complex case you solved)
        "!"     // Special character case (Should only count length)
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        run_test_case(test_cases[i]);
    }

    return 0;
}