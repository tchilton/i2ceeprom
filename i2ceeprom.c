// Application to read, write, fill and verify I2C EEPROM devices 

// Copyright Tim Chilton 26/08/2014 

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

// Version 0.1 29/08/2014

// Note that on a shared I2C bus, other controllers may be addressing the 
// same device, so always reset the address pointers before any data 
// transfers if the bus is busy, then our operations may fail, 
// so retry them a couple of times before giving up.

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Function prototypes
int     pollReady(int fh);
int     writeTo(int fh, int address, char * buf, int iolen);
int		readFrom(int fh, int address, char * buf, int iolen);
int		gotoAddress(int fh, int address);
void    usage(void);
int     checkValid(int size);
int     myatoi(const char *str);
int     hexDump(char * membuf, int size);
void    readDevice(int fh, char * membuf, int memSize);
void    writeDevice(int fh, char * membuf, int memSize);
int     verifyToBuffer(int fh, char * membuf);
void    readFileToBuffer(char * membuf, char * filename, int memSize);
void    writeFileFromBuffer(char * membuf, char * filename, int memSize);
int     openDevice(int bus, int device); 
void    fillBuffer(char * membuf, int pattern);

// Constants
#define MAXFILEPATH 250         // Maximum file name length

// Compile with this in for more verbose output
//#define DEBUGGING 1

// Globals
int	pageSize    = 32;		    // Number of bytes max for a page write
int sizek       = 4;            // Default device size in Kb
int	memSize;                   	// Size of the device in bytes

int main(int argc, char * argv[]) {
    char    filename[MAXFILEPATH] = { '\0' }; 
    int     busaddr = 1;            // I2C bus address
    int     i2caddr = 0x51;			// The I2C address of the device - EEPROM
    int     check;  
    int     i;
	int     fhand;					// Filename of the I2C device
    int     doFill=0;               // True if we are filling memory
    int     doRead=0;               // True if we are reading the device
    int     doWrite=0;              // True if we are writing the memory 
    int     doHexDump=0;            // True if we are hexdumping the memory to stdout
    int     doVerify=0;             // True if we are verifying a read/write operation
    int     pattern;                // The fill pattern
    char    * membuf;               // Memory buffer 

    memSize = sizek * 1024;

    // Handle the arguments
	if (argc < 3) {
		usage();
	}

	for(i=1; i<argc; i++) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {
				case 'h':               // Help
                    usage();

				case 'p':              // Set the page size
                    if (++i >= argc) { usage(); }
                    check  = myatoi(argv[i]);
                    if (checkValid(check) && check <=128) {
                        pageSize = check;
                    } else {
                        printf("Page Size should be a binary multiple such as 32, 64, 128 bytes\n");
                        exit(1);
                    }    
					break;

				case 's':              // Set the device size in Kb
                    if (++i >= argc) { usage(); }
                    check = myatoi(argv[i]);
                    if (checkValid(check) && check <=64) {
                        sizek = check;
                        memSize = sizek * 1024;
                    } else {
                        printf("Device size must be a binary multiple in the 1-64K range\n");
                        exit(1);
                    }
					break;

				case 'v':              // Do a verify after any operation
                    doVerify = 1;
					break;

				case 'f':              // Fill device with pattern
                    if (++i >= argc) { usage(); }
                    switch (argv[i][0]){
                        case '0':
                            pattern=0x00;
                            doFill=1;
                            break;

                        case '1':
                            pattern=0xff;
                            doFill=1;
                            break;

                        case '3':
                            pattern=-1;
                            doFill=1;
                            break;

                        case '5':
                            pattern=0x55;
                            doFill=1;
                            break;

                        case 'a':
                            pattern=0xaa;
                            doFill=1;
                            break;

                        default:
                            printf("Invalid Fill pattern\n");
                            usage();

                    }
                    doFill = 1;
					break;

				case 'd':              // Hexdump the devices contents
                    doHexDump=1;
					break;

				case 'w':              // Write file to EEPROM
                    doWrite =1;
                    break;

				case 'r':              // Read EEPROM to file
                    doRead=1;
					break;

				case 'n':              // Name of file  to use
                    if (++i >= argc) { usage(); }
                    if (strlen(argv[i]) < MAXFILEPATH) {
                        strcpy(filename,argv[i]);
                    } else {
                        printf("Filename is too long..\n");
                    }
					break;

                default:
                    usage();
                    break;

			}
		} else {
            switch (i) {                // Fixed position arguments
                case 1:
                    busaddr = myatoi(argv[i]);
                    break;

                case 2:
                    i2caddr =  myatoi(argv[i]);
                    break;

                default:
                    usage();
                    break;
            }
        }
    }


    // Check that all the related arguments were provided 

    if (!(doRead || doWrite || doVerify || doFill || doHexDump)) {
        printf("Nothing to do - check your options !\n");
        exit(1);
    }
    
    if (doFill) {       // Fill can do a verify without a filename
    } else if ((doRead || doWrite || doVerify) && (strlen(filename) == 0)) {
        printf("Filename not specified\n");
        exit(1);
    }


    // Do the work
    fhand = openDevice(busaddr,i2caddr);     // Open the device

	if (!(membuf= (char *) malloc(memSize+pageSize))) {  // Create the memory buffer
		printf("Malloc failed !");
		exit(1);
	}

    if (doFill) {                           // Fill the device with a pattern
        fillBuffer(membuf,pattern);
        writeDevice(fhand,membuf, memSize);
    }

    if (doWrite) {                          // Write the file to the EEPROM
        readFileToBuffer(membuf,filename, memSize);
        writeDevice(fhand,membuf, memSize);
    }

    if (doRead) {                           // Read the EEPROM to the file
        readDevice(fhand,membuf,memSize);
        writeFileFromBuffer(membuf, filename, memSize);
    }

    if (doVerify) {                         // Verify the EEPROM to the memory buffer
        if (!doFill && (!(doRead || doWrite) && doVerify)) {// Fill memory buffer if necessary
            readFileToBuffer(membuf,filename, memSize);
        }
        verifyToBuffer(fhand,membuf);
    }

    if (doHexDump) {                        // Hexdump the device out 
        printf("EEPROM contents\n\n");
        readDevice(fhand,membuf,memSize);
        hexDump(membuf, memSize);          
    }

    free (membuf);
    return (0);
}


// ******************
// Functions
// ******************


// Fill the device with the standard patterns
// The incremental pattern fills with an increasing value but offsets by +3 on 
// each 0x0100, this makes it possible to detect dead pages in a device
void fillBuffer(char * membuf, int pattern) {
    int     chunk;
    int     lp;
    char    * bptr;

    bptr = membuf;

    printf("Preparing pattern ");
    if (pattern == -1) {                        // Incremental pattern
        printf("(Increment)\n");
        for (chunk = 0 ; chunk < (memSize / 256) ; chunk++) {
            for(lp = 0 ; lp < 256 ; lp++) {		   
                *bptr++ = lp + (chunk * 3);     // Add 3 on each page       
            }
        }
	} else {
        printf("(0x%02x)\n",pattern);           // Fill of same value
        for (lp = 0 ; lp <= memSize ; lp++) {
            *bptr++ = pattern;
        }
    }
}


// Hex dump the memory buffer
// same as doing a hexdump -C on the saved file ..
int hexDump(char * membuf, int size) {
    int         address=0;                      
    int         rowlen = 16;
    char        buf[rowlen];
    char        *bptr;
    int         lp;

    bptr = membuf;

    printf("      00 01 02 03 04 05 06 07   08 09 0A 0B 0C 0D 0E 0F\n\n");

    while (address < size) {
        printf("%04X  ",address);                  // Address start

        for (lp = 0 ; lp< rowlen ; lp++) {
            if (lp == 8) { printf("  "); }
            printf("%02X ",*bptr);
            if (isprint(*bptr)) {
                buf[lp] = *bptr;
            } else {
                buf[lp] = '.';
            }
            bptr++;
        }
        address+=rowlen;

        // Print the characters out
        putchar(' ');
        putchar('|');
        for (lp = 0 ; lp < rowlen ; lp++) {
            if (lp == 8) { printf(" "); }
            if (isprint(buf[lp])) {
                putchar(buf[lp]);
            } else {
                putchar('.');
            }
        }
        putchar('|');
        putchar('\n');
    }
    return (0); 
}


// Write the file from the buffer
void writeFileFromBuffer(char * membuf, char * filename, int memSize) {
    FILE * fp;

    printf("Writing %s\n",filename);
    if (!(fp=fopen(filename,"wb"))) {
        printf("Unable to create %s\n",filename);
        exit(1);
    }

    if (!(fwrite(membuf,memSize,1,fp))) {
        printf("Write failed\n");
        exit(1);
    }

    fclose(fp);
}


// Read the file into the buffer
void readFileToBuffer(char * membuf, char * filename, int memSize) {
    FILE * fp;

    printf("Reading %s\n",filename);
    if (!(fp=fopen(filename,"rb"))) {
        printf("Unable to read %s\n",filename);
        exit(1);
    }

    if (fread(membuf,memSize,1,fp) != 1) {
        if (feof(fp)) {
            printf("Warning : file smaller than EEPROM (Remainder filled with 0x00)\n");
        } else {
            printf("Read failed\n");
            exit (1);
        }
    }

    fclose(fp);
}


// Verify the device to the buffer
int verifyToBuffer(int fh, char * membuf){
    int         address=0;              
    char        *vbuf;
    char        *vbp;
    char        *bp;
    int         errors=0;
    int         lp;

    bp=membuf;

	if (!(vbuf= (char *) malloc(pageSize+1))) {         // Create the memory buffer
		printf("Malloc failed !");
		exit(1);
	}
    vbp = vbuf;

    printf("Verifying.\n");
    fflush(stdout);

    while ((address < memSize) && (errors < 10)) {
        vbp=vbuf;

        if (readFrom(fh, address, vbp, pageSize)) {               // Read from the memory
            printf("Read from device failed\n");
            return (1);
        }


        // Verify the page
        for (lp = 0 ; (lp < pageSize) && (errors < 10) ; lp++) {
            if (*vbp != *bp) {
                printf("Verify error at 0x%04x read 0x%02x, expect 0x%02x\n",address,*vbp, *bp);
                if (++errors >= 10) {
                    printf("Ignoring other verify errors\n");
                }
            } 
            address++;
            bp++;
            vbp++;
        }

        putchar('.');
        fflush(stdout);
    }
    free(vbuf);
    if (errors) {
        printf("\nVerify failed\n");
    } else {
        printf("\nVerify OK\n");
    }
    return(errors);
}

// Read the device into the buffer
void readDevice(int fh, char * membuf, int memSize){
    int         address=0;              
    char        *bp;

    bp=membuf;

    printf("Reading device\n");
    fflush(stdout);

    while (address < memSize) {
        if (readFrom(fh, address, bp, pageSize)) {               // Read from the memory
            printf("Read from device failed\n");
            exit(1);
        }

        address+=pageSize;
        bp+=pageSize;
        putchar('.');
        fflush(stdout);
    }
    printf("\nDone\n");
}

// Write the device from the buffer
void writeDevice(int fh, char * membuf, int memSize){
    int         address=0;               
    char        *bp;
    bp=membuf;

    printf("Writing device.\n");
    fflush(stdout);

    while (address < memSize) {
        writeTo(fh, address, bp, pageSize);
        address+=pageSize;
        bp+=pageSize;
        putchar('.');
        fflush(stdout);
    }
    printf("\nDone\n");
}


// Open the connection to the device
int openDevice(int bus, int device) {
    char    filename[20];
    int     fh;

    printf("Opening device 0x%02x on bus %x...\n",device,bus);
    printf("Device is %dK with page size of %d bytes\n",sizek, pageSize);
    printf("\n");
    sprintf(filename,"/dev/i2c-%d",bus);		// Includes the I2C bus that the device is on
    if ((fh = open(filename,O_RDWR)) < 0) {
        printf("Failed to open the bus.\n");
        /* ERROR HANDLING; you can check errno to see what went wrong */
        exit(1);
    }

    if (ioctl(fh, I2C_SLAVE, device) < 0) {
        printf("Failed to acquire bus access and/or talk to slave.\n");
        /* ERROR HANDLING; you can check errno to see what went wrong */
        exit(1);
    }
    return (fh);
}

// Go to the specified address in the chip
int gotoAddress(int fh, int address) {
	char			buf[5];
	int				ret;

#ifdef DEBUGGING
    const char *	buffer;
#endif

    buf[0] = (address & 0xFF00) >> 8;
    buf[1] = address & 0xFF;

    pollReady(fh);                          // Wait until the chip is ready to do another operation
    if ((ret = write(fh, buf, 2)) != 2) {   // ERROR HANDLING: i2c transaction failed 
        #ifdef DEBUGGING
            printf("Failed to set the EEPROM address.\n");
            buffer = strerror(errno);
            printf(buffer);
            printf("\n\n");
        #endif
		return (1);
    }
	return (0);
}


// Write the specified buffer at the specified offset
// Ensure that we do not exceed the maximum page size of the device
// Since the I2C bus is a shared resource, we may fail to read if another device is 
// doing someething (including talking to our chip !)
int writeTo(int fh, int address, char * buf, int iolen) {
    char            * bp;
	char            * ptr;
	int		        toWrite = iolen;
    int             thisWrite;
    int             retries=0;
    int             success=0;

#ifdef DEBUGGING
    const char *	buffer;
#endif

	if (!(bp= (char *) malloc(pageSize+3))) {       // Biggest thing we can write is a page - plus an address
		printf("Malloc failed !");
		exit(1);
	}

//    printf("Write at address 0x%04x for 0x%04x bytes\n",address, iolen);

	while (toWrite >0) {                        // Still data to write
        pollReady(fh);                          // Wait until the chip is ready to do another operation
        thisWrite= 0;                           // an empty page
        ptr = bp;                               // Prepare the next page of data
        *ptr++ = (address & 0xFF00) >> 8;		// Move to the specified address
        *ptr++ = address & 0xFF;
    
        while (toWrite >0 && (thisWrite < pageSize)) {  // Fill the buffer
            *ptr++ = *buf++;
            thisWrite++;
            toWrite--;
        }
        thisWrite+=2;                           // Add on the address to the size
        success=0;
        retries=0;
        while (!success && retries < 100) {
            if (write(fh, bp, thisWrite) != thisWrite) {        // Try to write the data
                #ifdef DEBUGGING
                    printf("Failed to write to i2c bus.\n");    
                    buffer = strerror(errno);
                    printf(buffer);
                    printf("\n\n");
                #else
                    putchar('E');               // Indicate error on output
                    fflush(stdout);
                #endif
                usleep(10);	                    // 10us delay
                retries++;

            } else {
                success=1;
            }
        }

        if (!success) {
            printf("\nHard write error - aborting\n");
            exit (2);
        }

        thisWrite-=2;               // Remove the address offset from the buffer again
        address += thisWrite;       // Update the address of the next write
	}
    pollReady(fh);              // Wait until the chip is ready to do another operation
	free (bp);
    return (success);
}

// Read from the specified device into the provided buffer
// It seems that the I2C driver subsystem or devices can't handle read beyond 1K,
// so use smaller reads ..
// Since the I2C bus is a shared resource, we may fail to read if another device is 
// doing someething (including talking to our chip !)

int	readFrom(int fh, int address, char * buf, int iolen) {
    int             bytesRead;
    int             retries=0;
    int             success=0;

#ifdef DEBUGGING
    const char *	buffer;
#endif

    if (iolen >1024) {
        printf("Maximum IO length is 1K. use a smaller read\n");
        return (-1);
    }

    pollReady(fh);                                                  // Wait until the chip is ready to do another operation
    while (!success && retries++ <100) {
        if (gotoAddress(fh,address)) {
            #ifdef DEBUGGING
                printf("Failed to goto address 0x%04x\n",address);
            #else
                putchar('E');                                       // Indicate error on output
                fflush(stdout);
            #endif
            usleep(10);	                                            // 10us delay
        } else {
            if ((bytesRead = read(fh, buf, iolen)) != iolen) {      // I2C Read
                #ifdef DEBUGGING
                    printf("Failed to read from the i2c bus.\n");   // ERROR HANDLING: i2c transaction failed 
                    buffer = strerror(errno);
                    printf(buffer);
                    printf("\n\n");
                #else
                    putchar('E');                                   // Indicate error on output
                    fflush(stdout);
                #endif
                usleep(10);	                                        // 01us delay
            } else {
                success=1;
            }
        }
    }     
    if (!success) {
        printf("\nHard read error - aborting\n");
        exit (1);
    }
    return (0);
}

// Poll for the device being ready. This is done by performing a single byte read
// the device will fail to acknowledge whilst it is still busy writing

int pollReady(int fh) {
	char            buf[2];
    int             timeout=100;

    // Poll for the device coming ready after a write - reads cant be performed whilst a write is occurring
    while (read(fh,buf,1) != 1) {
        usleep(1);	                                        // 1us delay
        if (timeout-- == 0) {
            return (1);                                     // If its taken this long then the chip is dead
        }
    }
    return (0);
}

// Print instructions for the user
void usage() {
    printf("Utility to manipulate I2C EEPROM devices\n\n"); 
	printf("Usage: i2ceeprom <i2c-bus> <i2c-addr> [options]\n");
	printf("Options:\n"
	       "  -h                Print this help.\n"
	       "  -p <page-size>    Page size of device. default is 32 bytes.\n"
	       "  -s <dev-size>     Set the device size in Kb. 1-64 Kb.\n"
	       "  -f <pattern>      Fill device with specified pattern.\n"
	       "        0 - All zero's (0x00).\n"
	       "        1 - All one's  (0xFF).\n"
	       "        3 - Incremental pattern with +3 offset on each page (0x100)\n"
	       "        5 - b01010101  (0x55).\n"
	       "        a - b10101010  (0xAA).\n"
	       "  -d                Dump (read) device and hex dump to stdout.\n"
	       "  -w                Write file contents into EEPROM.\n"
	       "  -r                Read contents of EEPROM into file.\n"
	       "  -v                Verify after operation (includes fill)\n"
	       "  -n <file>         Filename to be used for operation.\n"
	       "\n\n"
	       "Whilst processing, real-time output will be produced\n"
           "  . means progress without error (one dot per page)\n"
           "  E means transient bus read error (one E per error)\n"
           "    for example another device is mastering the I2C bus\n"
           "\n"
           "See manpage for full information\n"
	       "\n");
    exit(1);
}


// atoi that accepts both hex and decimal numbers
int myatoi(const char *str) {
	if ((str[0] == '0') && (str[1]=='x')) {
		return (int)strtol(str+2, NULL, 16);
    } else {
        return (int)strtol(str, NULL, 10);
    }
}

// Check if the passed parameter is a binary heading
// Used to check that the parameters passed are sensible
int checkValid(int size) {
    int     heading;
    int     maxHead = 1<<14;

    if (size==1) return(1);

    for (heading=1 ; heading < maxHead ; heading<<=1) {
        if (size == heading) {
            return (1);
        }
    }
    return (0);
}
