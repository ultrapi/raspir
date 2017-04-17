#include <stdio.h>
#include <wiringPi.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

#include <stdbool.h>
#include <unistd.h>

//Simple IR sender for Raspberry PI
//Tested with Mitsubishi Heatpump MSZ-FH35VE and Raspberry PI model B 
//Requires install of wiringPi 
//Compile with "gcc -Wall -o raspir raspir.c -lwiringPi"

typedef struct {
	int gpioPin; //Output GPIO pin number
	char isSample;
	char* outFile;
	char* inFile;
	int trig;
	int statisticsEnable;
	char* hex; //Hex code to send
	int verbose;
    int decode;
} Arg;

Arg getArgDefault() {
	Arg arg = {
			.gpioPin=-1, //Disabled by default
			.isSample=0,
			.trig=0,
			.outFile=0,
			.inFile=0,
			.statisticsEnable=0,
			.hex=0,
			.verbose=0,
			.decode=0
	};
	return arg;
}

Arg getArg(int argc, char** argv) {

	Arg arg = getArgDefault();

	int error=0;
	int opt;
	while ((opt = getopt(argc, argv, "vstmdp:o:i:h:")) != -1) {
		switch (opt) {
		case 'p': arg.gpioPin=atoi(optarg); break;
		case 's': arg.isSample=1; break;
		case 't': arg.trig=1; break;
		case 'i': arg.inFile=optarg; break;
		case 'o': arg.outFile=optarg; break;
		case 'm': arg.statisticsEnable=1; break;
		case 'h': arg.hex=optarg; break;
		case 'v': arg.verbose=true; break;
		case 'd': arg.decode=true; break;
		default:
			error=1;
		}
	}

	if (arg.isSample && arg.gpioPin<0) {
		printf("GPIO pin number (-p) must be specified with -s\n");
		error=1;
	}

	if (argc<2 || error) {
		printf("Usage: %s [OPTIONS]...\n Arguments: (one of -s or -i or -h is mandatory)\n",argv[0]);
		printf("  -p   GPIO pin number\n");
		printf("  -s   sample GPIO pin\n");
		printf("  -t   trig any change on GPIO pin before starting sample\n");
		printf("  -o   output file for samples\n");
		printf("  -i   input file to send IR on GPIO pin (same format as -o flag)\n");
		printf("  -h   Hex data to send (for example DEADBEEF1234)\n");
		printf("  -m   measure statistics at send (may be slower)\n");
		printf("  -d   decode sample data to hex (Mitsubishi Heatpump MSZ-FH35VE)\n");
		printf("  -v   verbose, print info\n");

		exit(EXIT_FAILURE);
	}

	return arg;
}


void printArg(Arg arg) {
	printf("arg:\n");
	printf("gpioPin=%i\n",arg.gpioPin);
	printf("isSample=%i\n",arg.isSample);
	printf("trig=%i\n",arg.trig);
	printf("outFile=%s\n",arg.outFile);
	printf("inFile=%s\n",arg.inFile);
	printf("hex=%s\n",arg.hex);
	printf("decode=%i\n",arg.decode);
	printf("verbose=%i\n",arg.verbose);
}

int isEqual(unsigned char* bytesA, int byteCountA, unsigned char* bytesB, int byteCountB) {
	if (byteCountA!=byteCountB) return 0;
	int i;
	for (i=0;i<byteCountA-1;i++) if (bytesA[i]!=bytesB[i]) return 0;
	return 1;
}


//Decode raw data for Mitsubishi Heatpump to bytes
unsigned char* decodeRawData(int* sampleTime, int* sample, int sampleCount, int* resultByteCount) {

	//Remove all 101 high frequence pulses to zero
	int shortTimeLimit = 200; //[µs] Any 101 shorter than this is considered a hi freq pulse
	int i;
	for (i=0;i<sampleCount-2;i++) {
		if (sample[i]==1&&sample[i+1]==0&&sample[i+2]==1&&(sampleTime[i+2]-sampleTime[i+1]<shortTimeLimit)) {
			sample[i+1]=1;
		}
	}

	//Remove all duplicates
	int j=0;
	for (i=0;i<sampleCount-1;i++) {
		if (sample[i]!=sample[i+1]) {
			sampleTime[j+1]=sampleTime[i+1];
			sample[j+1]=sample[i+1];
			j++;
		}
	}
	sampleCount=j;

	int oneZeroLevel=(400+1200)/2; //Below level is zero, above is 1

	int preByteCount=sampleCount/8+1;
	unsigned char* bytes=malloc(preByteCount);

	const int bitCount=8;

	int bc=0;
	int byteIndex=0;

	//Get bits
	unsigned char byte=0;
	int lastIndex=-1;
	for (i=3;i<sampleCount-1;i++) {
		if (i%2==0) {
			int dt=sampleTime[i+1]-sampleTime[i];
			int bit=oneZeroLevel<dt;
			bc++;
			byte>>=1;
			byte+=bit<<7;
			if (bitCount<=bc) {
				bytes[byteIndex++]=byte;
				byte=0;
				bc=0;
			}
			if (2000<dt) {
				//printf("END at %i\n",i);
				lastIndex=i;
				break;
			}
		}
	}

	int byteCount=byteIndex;

	//Verify checksum
	unsigned char crc=0;
	for (i=0;i<byteCount-1;i++) crc+=bytes[i];
	int crcOK = crc==bytes[byteCount-1];

	if (!crcOK) {
		printf("Error in checksum, calculated %i found %i\n", crc, bytes[byteCount-1]);
	}

	//Check if only read half, then read the repeated data
	if (sampleCount*.4<lastIndex && lastIndex<sampleCount*.6) {
		int byteCount2;
		unsigned char* byte2 = decodeRawData(sampleTime+lastIndex, sample+lastIndex, sampleCount-lastIndex+1, &byteCount2);

		//Check that 2nd transmit is identical
		if (byteCount!=byteCount2) {
			printf("Byte count differ in 2nd transmit %i!=%i\n",byteCount,byteCount2);
		} else if (!isEqual(bytes, byteCount, byte2, byteCount2)) {
			printf("2nd transmit not identical\n");
		}

		free(byte2);
	}

	*resultByteCount=byteCount;
	return bytes;
}

char* getHex(unsigned char* byte, int byteCount) {
	char* hex = malloc(byteCount*2+1);
	hex[byteCount*2]=0;
	int i;
	for (i=0;i<byteCount;i++) {
		sprintf(hex+i*2,"%02X",byte[i]);
	}
	return hex;
}

//Return number of samples saved. Saving only sample changes.
int writeSamples(char* outFile, int* sampleTime, int* sample, int sampleCount) {
	char includeFlank = 0; //Set to true to include flank to look better in excel scatter chart
	char verbose = 0;
	FILE *file = fopen(outFile,"w");
	if (verbose) printf("[µs]\tSample\n");
	fprintf(file,"[µs]\tSample\n");
	int i=0;
	int lastSample=-1;
	int savedSampleCount = 0;
	while(i<sampleCount) {
		if (lastSample!=sample[i]) {
			if (verbose) printf("%i\t%i\n",sampleTime[i], sample[i]);
			if (includeFlank && 0<=lastSample) fprintf(file,"%i\t%i\n",sampleTime[i], lastSample);
			fprintf(file,"%i\t%i\n",sampleTime[i], sample[i]);
			lastSample=sample[i];
			savedSampleCount++;
		}
		i++;
	}
	fclose(file);
	return savedSampleCount;
}


void sampleGpio(Arg arg) {

	if (arg.verbose) printf("Sampling pin %i\n", arg.gpioPin);
	pinMode (arg.gpioPin, INPUT);
	pullUpDnControl(arg.gpioPin,PUD_OFF);

	long i=0;

	const int sampleSize=1000000; //Raspberry PI 2 has 1GB RAM
	int samples[sampleSize];
	int sampleTime[sampleSize];
	samples[i] = digitalRead(arg.gpioPin);

	if (arg.trig) {
		if (arg.verbose) printf("Waiting for trig on pin %i...\n",arg.gpioPin);
		while(samples[i] == digitalRead(arg.gpioPin));
		sampleTime[i++] = micros();
		if (arg.verbose) printf("Trigged!\n");
	}

	//Sample as fast as possible
	piHiPri(99); //High priority process
	while(i<sampleSize) {
		samples[i] = digitalRead(arg.gpioPin);
		sampleTime[i] = micros();
		i++;
	}
	piHiPri(0); //Back to normal prio

	//Find last sample index to save, that is last changed sample
	int lastSampleIndex = sampleSize-1;
	while(0<lastSampleIndex && samples[lastSampleIndex-1]==samples[sampleSize-1]) lastSampleIndex--;

	if (arg.verbose) printf("%i samples, keeping only first %i samples as all following did not change\n",sampleSize, lastSampleIndex+1);

	int sampleCount=lastSampleIndex+1;
	int firstTime=sampleTime[0];

	//Set start time to zero
	for (i=0;i<sampleCount;i++) sampleTime[i]-=firstTime;

	if (arg.outFile) {
		int savedSampleCount = writeSamples(arg.outFile, sampleTime, samples, sampleCount);
		if (arg.verbose) printf("Wrote %i sample%s to file %s, including only sample changes\n",savedSampleCount, savedSampleCount==1?"":"s", arg.outFile);
	}

	//Print hex
	if (arg.decode) {
		int byteCount;
		unsigned char* byte = decodeRawData(sampleTime, samples, sampleCount, &byteCount);
		char* hex = getHex(byte,byteCount);
		if (arg.verbose)   printf("decoded hex=");
		printf("%s\n",hex);
	}
}

int getFileLineCount(char* fileName) {
	int lineCount =0;
	FILE *file = fopen(fileName,"r");
	while(!feof(file)) {
		if (fgetc(file)=='\n') lineCount++;
	}
	return lineCount;
}

//micros() wraps about every 71 minutes. After this call there will be at least n seconds before next wrap.
//May not be needed as micros() appear to start at zero at program start
void avoidWrapMicrosecondClock(int secondsMargin) {
	while(0xFFFFFFFF-secondsMargin*1e6<micros());
}

void sendData(int* sampleTime, int* sample, int sampleCount, int gpioPin, int statisticsEnable) {
	pinMode (gpioPin, OUTPUT);

	int i;
	int resultTestTime[50000];

	int lastTime = sampleTime[sampleCount-1];

	//Measure time to recalculate time column
	int test[1];
	int t0=micros();
	for (i=0;i<sampleCount;i++) test[i]+=lastTime;
	int recalcTime = micros()-t0;
	int startRealTime = micros()+recalcTime*200;

	//Adjust sampleTime to match current microsecond timing
	//This is to avoid any time consuming steps within time-critical loop
	for (i=0;i<sampleCount;i++) sampleTime[i]+=startRealTime;

	int t;

	//This may be optimized by removing t and resultTestTime[i] from inside the loop
	piHiPri(99); //High priority process
	i=0;
	if (statisticsEnable) {
		while (i<sampleCount) {
			while ((t=micros())<sampleTime[i]); //Wait until next sample
			digitalWrite(gpioPin, sample[i]);
			resultTestTime[i]=t;
			i++;
		}
	} else {
		//Same as above if-case but faster, not measuring statistics
		while (i<sampleCount) {
			while (micros()<sampleTime[i]); //Wait until next sample
			digitalWrite(gpioPin, sample[i]);
			i++;
		}
	}
	piHiPri(0); //Back to default


	if (statisticsEnable) {
		//Print timing statistics
		int maxDiffMicros = 0;
		long diffSum=0;
		for (i=0;i<sampleCount;i++) {
			diffSum+=resultTestTime[i]-sampleTime[i];
			if (resultTestTime[i]-sampleTime[i]<0) {
				printf("NEGATIVE time diff at time %iµs\n",sampleTime[i]);
			}
			if (maxDiffMicros<resultTestTime[i]-sampleTime[i]) {
				maxDiffMicros=resultTestTime[i]-sampleTime[i];
			}
		}
		printf("max time diff=%i µs, average=%f µs\n", maxDiffMicros, diffSum/(double)sampleCount);
	}
}

//Result length is length of hex string divided by two
//Caller should free returned array
unsigned char* getByteFromHex(char* hex) {
	int len=strlen(hex)/2;
	unsigned char* result=malloc(len);
	char* pos=hex;
	int count;
	for(count = 0; count < len; count++) {
		sscanf(pos, "%2hhx", &result[count]);
		pos += 2;
	}
	return result;
}


//Add pulses, return consumed samples of sampleTime
//sampleTime first point is already set as starting point and samples is added after this
int addSample(int* sampleTime,int* sample, int hTime, int lTime, int pulseHLength, int pulseLLength) {
	int pos = 0;
	int startTime = sampleTime[pos];
	int t = startTime;
	while (t+hTime+lTime<=startTime+pulseHLength) {          
		sample[pos]=1;
		pos++;
		sampleTime[pos]=t+hTime;
		sample[pos]=0;
		pos++;
		sampleTime[pos]=t+hTime+lTime;
		t+=hTime+lTime;
	}
	sampleTime[pos]=t+pulseLLength;
	return pos;
}

void printHex(unsigned char* bytes, int length) {
	int i;
	for(i = 0; i < length; i++) printf("%02x ", bytes[i]);
	printf("\n");
}

//Generate raw timed bit blink flow for IR lamp, Mitsubishi Heatpump MSZ-FH35VE
//Caller should free returned array result
void generateRaw(char* hex, int** sampleTimeResult, int** sampleResult, int *sampleCountResult) {

	unsigned char* bytes = getByteFromHex(hex);
	int byteLen = strlen(hex)/2;

	//Timing of pulses [µs], measured
	int hTime=15; //Time for short high pulse
	int lTime=11; //Time for short low pulse
	int initOffsetTime=700;
	int initPulseHLength=3344;
	int initPulseLLength=1688;
	int bitHLength=428; //Normal 1 bit high time
	int oneBitLLength=1268; //Normal 1 bit low time
	int zeroBitLLength=430; //Normal 0 bit low time
	int repeatePause=11354;

	int bitCount=byteLen*8;
	int sampleGuessCount = (bitHLength+oneBitLLength)*bitCount/(hTime+lTime)*1.1; //Pessimistic estimation
	int* sampleTime = malloc(sampleGuessCount*sizeof(int));
	int* sample = malloc(sampleGuessCount*sizeof(int));

	//Init at zero
	sampleTime[0]=0;
	sample[0]=0;
	//Init next bit start time
	sampleTime[1]=initOffsetTime;
	int pos=1;
	int byteIndex;
	int bitIndex;
	int repeatIndex;
	for (repeatIndex=0;repeatIndex<2;repeatIndex++) {
		pos += addSample(sampleTime+pos,sample+pos, hTime, lTime, initPulseHLength, initPulseLLength);
		for (byteIndex=0;byteIndex<byteLen;byteIndex++) {
			unsigned char b=bytes[byteIndex];
			for (bitIndex=0;bitIndex<8;bitIndex++) {
				unsigned char bitVal = ((b >> bitIndex)  & 1);
				int lLength=bitVal?oneBitLLength:zeroBitLLength;
				pos += addSample(sampleTime+pos,sample+pos, hTime, lTime, bitHLength, lLength);
			}
		}
		//Stop bit
		pos += addSample(sampleTime+pos,sample+pos, hTime, lTime, bitHLength, 0);
		//Repeat pause
		pos += addSample(sampleTime+pos,sample+pos, hTime, lTime, 0, repeatePause);
	}
	sample[1]=1;
	sample[0]=0;
	*sampleTimeResult=sampleTime;
	*sampleResult=sample;
	*sampleCountResult=pos;
}

void sendHex(Arg arg) {
	int* sampleTime;
	int* sample;
	int sampleCount;
	generateRaw(arg.hex, &sampleTime, &sample, &sampleCount);

	if (0<=arg.gpioPin) {
		sendData(sampleTime, sample, sampleCount, arg.gpioPin, arg.statisticsEnable);
	}

	if (arg.outFile) {
		int savedSampleCount = writeSamples(arg.outFile, sampleTime, sample, sampleCount);
		if (arg.verbose) printf("Wrote %i sample%s to file %s, including only sample changes\n",savedSampleCount, savedSampleCount==1?"":"s", arg.outFile);
	}

	free(sampleTime);
	free(sample);
}


void readSampleFile(Arg arg) {
        if (access(arg.inFile, F_OK)==-1) {
           printf("File not found: %s\n",arg.inFile);
           exit(EXIT_FAILURE);
        }

	int lineCount = getFileLineCount(arg.inFile);

	FILE *file = fopen(arg.inFile,"r");
	if (file == NULL) {
		printf("Error opening file %s\n", arg.inFile);
		return;
	}

	int sampleCount = lineCount-1; //Ignore header line
	int* sample = malloc(sampleCount*sizeof(int));
	int* sampleTime = malloc(sampleCount*sizeof(int));

	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	read = getline(&line, &len, file); //Header
	int i=0;
	while ((read = getline(&line, &len, file)) != -1) {
		char* millisText = strtok(line,"\t");
		char* pinStateText = strtok(NULL,"\t");
		if (millisText==0 || pinStateText==0) {
			printf("Bad input at line %i\n",i);
			return;
		}
		sample[i]=atoi(pinStateText);
		sampleTime[i]=atoi(millisText);
		i++;
	}
	fclose(file);

	if (0<=arg.gpioPin) {
		sendData(sampleTime, sample, sampleCount, arg.gpioPin, arg.statisticsEnable);
	}

	//Print hex
	if (arg.decode || arg.outFile) {
		int byteCount;
		unsigned char* byte = decodeRawData(sampleTime, sample, sampleCount, &byteCount);
		char* hex = getHex(byte,byteCount);
		printf("%s\n",hex);

		if (arg.outFile) {
			Arg argSaveOnly = {
					.gpioPin=-1, //Do not send
					.isSample=0,
					.trig=0,
					.outFile=arg.outFile,
					.inFile=0,
					.statisticsEnable=0,
					.hex=hex,
					.verbose=arg.verbose
			};
			sendHex(argSaveOnly);
		}
	}

	free(sample);
	free(sampleTime);
}

int main (int argc, char** argv)
{
	Arg arg = getArg(argc, argv);

	wiringPiSetup () ;
	wiringPiSetupGpio () ;

	avoidWrapMicrosecondClock(2);

	if (arg.isSample) {
		sampleGpio(arg);
	} else if (arg.inFile) {
		readSampleFile(arg);
	} else if (arg.hex) {
		sendHex(arg);
	} else {
		printf("Nothing to do!\n");
	}
	return 0;
}


