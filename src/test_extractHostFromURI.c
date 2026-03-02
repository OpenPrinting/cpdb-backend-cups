/*
 * Unit tests for extractHostFromURI helper function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "test-internal.h"
#include "backend_helper.h"

// Test extractHostFromURI function
int main(int argc, char *argv[])
{
    char *host;
    
    printf("=== Unit Tests for extractHostFromURI ===\n\n");

    // Test 1: Valid HTTP URI with path
    testBegin("extractHostFromURI with HTTP URI and path");
    host = extractHostFromURI("http://example.com/path/to/resource");
    testEndMessage(host != NULL && strcmp(host, "example.com") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 2: Valid IPP URI with port
    testBegin("extractHostFromURI with IPP URI and port");
    host = extractHostFromURI("ipp://localhost:631/");
    testEndMessage(host != NULL && strcmp(host, "localhost") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 3: Valid HTTPS URI with no path
    testBegin("extractHostFromURI with HTTPS URI and no path");
    host = extractHostFromURI("https://printer.example.com");
    testEndMessage(host != NULL && strcmp(host, "printer.example.com") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 4: URI with port and path
    testBegin("extractHostFromURI with port and path");
    host = extractHostFromURI("http://192.168.1.100:8080/printers/laser");
    testEndMessage(host != NULL && strcmp(host, "192.168.1.100") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 5: URI with just scheme and host
    testBegin("extractHostFromURI with minimal URI");
    host = extractHostFromURI("ipp://host");
    testEndMessage(host != NULL && strcmp(host, "host") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 6: Invalid URI - no scheme
    testBegin("extractHostFromURI with invalid URI (no scheme)");
    host = extractHostFromURI("example.com/path");
    testEndMessage(host == NULL, "host should be NULL, got '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 7: Invalid URI - empty
    testBegin("extractHostFromURI with empty URI");
    host = extractHostFromURI("");
    testEndMessage(host == NULL, "host should be NULL, got '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 8: Invalid URI - NULL input
    testBegin("extractHostFromURI with NULL input");
    host = extractHostFromURI(NULL);
    testEndMessage(host == NULL, "host should be NULL, got '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 9: URI with IPv6 format (if supported)
    testBegin("extractHostFromURI with IPv6-like format");
    host = extractHostFromURI("http://[2001:db8::1]:631/");
    testEndMessage(host != NULL, "IPv6 host extraction, got '%s'", host ? host : "NULL");
    if (host) free(host);

    // Test 10: URI with complex subdomain
    testBegin("extractHostFromURI with complex subdomain");
    host = extractHostFromURI("https://print-server.department.company.com:443/ipp");
    testEndMessage(host != NULL && strcmp(host, "print-server.department.company.com") == 0,
                  "host = '%s'", host ? host : "NULL");
    if (host) free(host);

    printf("\n=== Test Summary ===\n");
    if (testsPassed) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("Some tests FAILED!\n");
        return 1;
    }
}
