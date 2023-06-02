//***************************************************************
//
//  StreamEvent.cpp
//
//  Simple program to demonstrate using the pacpci2.dll in a
//  program.
//  Compile this source file and link with PACPCI2.LIB
//  Copy the resulting program to a folder with PACPCI2.DLL and
//  PCIAPI.DLL.
//  Run the program (requires a PAC PCI-2 board with the
//  current Windows Drivers for the board installed on the
//  computer on which the program is run).
//
//***************************************************************

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

#include "pacp2lv.h"
#include "pacpci2.h"


#define MAXMSGLEN   32767
static byte msg[MAXMSGLEN];

// local function prototypes
int _tmain(void);
static void setupPCI2(int nChannels, int wfXfer);
static void dispMsg(byte *msg, short showHit, short showWfm, short showTDD);

// keyboard key defines
#define F7  0x4100
#define F8  0x4200
#define F9  0x4300      
#define F10 0x4400

// 1 PCI2 board, change to 4 if there are 2 board, 6 if 3...
#define MAX_AE_CHANNELS 2

#define ENABLE 1
#define DISABLE 0

// Lowpass and highpass filter selection settings
#define HIGHPASS_INDEX 2   // 0 =   1k, 1 =  5k, 2 =  20k, 3 = 100kHz
#define LOWPASS_INDEX  0   // 0 = 400k, 1 = 50k, 2 = 100k, 3 = 200kHz

static int number_of_channels = 0;
static int forced_hit = 0; 


// ID_PAUSE: Send to pause acquisition (pauseTest() does this), the acquisition board will send this id back
//			(with a time of test) when all data has been read out of its buffers.
//			Note, no matter how many boards are installed you receive only one back from the library.

// ID_ABORT: Send this to abort acquisition (abortTest()).  No message is returned, no buffers on the board
//           are flushed.

// ID_RESUME:  Send this to start a test (startTest()) or to resume after pausing (resumeTest()). The
//             acquisition board will send this id back (with a time of test) after it is received.

// ID_STOP:   Send this to stop acquistion (stopTest()). Send this only after using pauseTest() and the
//            pause message has been received back.  This is the 'proper' way to end acquisition.

// PAC command message id's
#define ID_PAUSE     130
#define ID_ABORT      15
#define ID_RESUME    128
#define ID_STOP      129

DWORD WINAPI streamThread(LPVOID arg);
static void setupPCI2(int nChannels, int wfXfer);

// Structure passed to thread
typedef struct {
      HANDLE startStream;
      HANDLE stopStream;
      HANDLE exitThread;
      int channels;
      float *channelData[20];
      unsigned long channelDataLength[20];
} threadData;


//***************************************************************
//
//
//
//
//***************************************************************

int _tmain()
{
    short done = 0;
    unsigned short key;
    int ecode;
    int i /*c*/;

    // Must first open the board.  If it fails then the support files
    // listed in the header at the top of this file could be missing.
	if ( key = openPCI2() )
    {
        _tprintf(_T("Could not open PCI2 (%d) ... Hit a key to exit.\n"), key);
        if ( !_getch() ) _getch();
        return(0);
    }

    // determine the number of channels avaliable.
    for ( i=1; i<=MAX_AE_CHANNELS; i++ )
    {
        if ( !checkChannelHardwarePresent(i)  )
            break;

        number_of_channels = i;
    }

    // No channels? Exit.
    if ( number_of_channels == 0 )
    {
        _tprintf(_T("Could not find any PCI2 channels ... Hit a key to exit.\n"));
        if ( !_getch() ) _getch();
        return(0);
    }

    // Current waveform transfer state
    short wfXfer = ENABLE;

    // Setup operating parameters for each channel.
    setupPCI2(number_of_channels, wfXfer);


	// Waveform Streaming Setup

	// Stream at a constant interval
    setWaveformStreamingMode(STREAMTRIG_TIMED);     // defines in PACP2LV.H
	setWaveformStreamingPeriod(5);	                // 5sec between streams
	setWaveformStreamingFilePrefix("EXAMPLE");	// first part of filename, second part is time/date
    
    // We don't want a WFS file (so we really do not need the setWaveformStreamingFilePrefix() call above)
    enableWFSOutput(0);

	// Set streaming for 2 channels
	setWaveformStreamingChannel(1, TRUE);		// stream on channel 1
	setWaveformStreamingChannel(2, TRUE);		// stream on channel 2

	unsigned long wfStreamLength_k = 5;                     // 5k total length
    setWaveformStreamingLength(1, 2, wfStreamLength_k);		// channel 1, 2ksample pretrigger,
	setWaveformStreamingLength(2, 2, wfStreamLength_k);		// channel 2, 2ksample pretrigger

	enableWaveformStreaming(TRUE);              // has to be enabled to work

    // Based on the values sent to setWaveformStreamingLength(), the driver will
    // calculate how many samples are required.  You MUST call setWaveformStreamingLength()
    // before getRequiredSampleBufferLen()
    unsigned long b1Len = getRequiredSampleBufferLen(1);
    unsigned long b2Len = getRequiredSampleBufferLen(2);

    // Allocate buffers for streamed data
    float *b1 = new float[b1Len];
    float *b2 = new float[b2Len];

    // Give the buffers to the driver
    setStreamingBuffer(1, b1, b1Len, 1);
    setStreamingBuffer(2, b2, b1Len, 1);

    // Create a manual-reset event object. The master thread sets
    // this to nonsignaled when it writes to the shared buffer.

    HANDLE hStartStreamEvent = CreateEvent(
        NULL,           // default security attributes
        TRUE,           // manual-reset event
        FALSE,          // initial state is not signaled
        "StartStream"   // object name
        );

    if ( hStartStreamEvent == NULL )
    {
        _tprintf(_T("CreateEvent failed (%d)\n"), GetLastError());
        closePCI2();
        return(0);
    }

    HANDLE hStopStreamEvent = CreateEvent(
        NULL,           // default security attributes
        TRUE,           // manual-reset event
        FALSE,          // initial state is not signaled
        "StopStream"    // object name
        );

    if (hStopStreamEvent == NULL)
    {
        _tprintf(_T("CreateEvent failed (%d)\n"), GetLastError());
        CloseHandle(hStartStreamEvent);
        closePCI2();
        return(0);
    }


    HANDLE hExitThread = CreateEvent(
        NULL,               // default security attributes
        TRUE,               // manual-reset event
        FALSE,              // initial state is not signaled
        "ExitStreamThread"  // object name
        );

    if (hExitThread == NULL)
    {
        _tprintf(_T("CreateEvent failed (%d)\n"), GetLastError());
        CloseHandle(hStartStreamEvent);
        CloseHandle(hStopStreamEvent);
        closePCI2();
        return(0);
    }

    // Give the events objects to the driver so the driver can signal them when required
    setStreamingEvents(hStartStreamEvent, hStopStreamEvent);

    // Create structure to pass to thread
    threadData td;
    td.exitThread = hExitThread;
    td.startStream = hStartStreamEvent;
    td.stopStream = hStopStreamEvent;
    td.channels = 1;
    td.channelData[0] = b1;
    td.channelDataLength[0] = wfStreamLength_k*1024;
    
    // Create the thread to act on the streamed data
    DWORD id;      
    HANDLE shellH = CreateThread(NULL, 0, streamThread, &td, 0, &id);
    if (shellH == NULL)
    {
        _tprintf(_T("CreateThread failed (%d)\n"), GetLastError());
        CloseHandle(hStartStreamEvent);
        CloseHandle(hStopStreamEvent);
        CloseHandle(hExitThread);
        closePCI2();
        return(0);
    }

    validateSetup();

    short running = 1;
    int astDone = 0;

    while ( running )
    {
        short done = 0;
        short waitForEnd = 0;
        
        // Flags to control what messages go to the display
        short showTDD = 1;
        short showHit = 1;
        short showWfm = 1;

        // Start collecting data
        if ( ecode = startTest() )
        {
            _tprintf("Could not Start Test (error %d).. Hit a key.\n", ecode);
            if ( !_getch() ) _getch();
            CloseHandle(hStartStreamEvent);
            CloseHandle(hStopStreamEvent);
            CloseHandle(hExitThread);
            closePCI2();
            return(0);
        }

        // Use F9 to pause data collection, followed by F10 to end it and exit
        // this program
        do {

            Sleep(0);       // give threads a chance to run

            if ( _kbhit() )
            {
                if ( (key = _getch()) == 0 )
                    key = _getch() << 8;

                switch ( key ) {

                case F7:
                {
                    // AST with 1 board
                    short channels[] = {1,2};
                    
                    startAST_Ex(channels,      // channel list 
                              2,               // channel count
                              10,              // pulse_count
                              5,               // pulse width
                              100,             // pulse_interval                                                   
                              1,               // gen output file
                              "test.ast",      // output file name                         
                              0,               // compare output
                              "reference.ast", // compare file
                              10,
                              &astDone);       // complete flag
                    break;
                }
                case F8: // force a hit and calculate partial powers
                    forceTrigger(1); 
                    forced_hit = 1;
                    break;

                case F9:
                    if ( pauseTest() )
                        resumeTest();
                    break;

                case F10:
                    if ( stopTest() )
                    {
                        abortTest();    // not paused, do an abort instead
                        waitForEnd = 2; // we need to write an abort message to the data file
                    }
                    else
                        waitForEnd = 1; // wait for stop message from dll before ending
                    break;

                case 'p':
                case 'P':
                    pulseChannelAST(1);
                    break;            

                case 't':
                case 'T':
                    sendTimeMark();
                    break;

                case 'w':
                case 'W':
                    if ( wfXfer )
                    {
                        stopWaveformTransfer();
                        wfXfer = 0;
                    }
                    else
                    {
                        startWaveformTransfer();
                        wfXfer = 1;
                    }
                    break;

                case 'h':
                case 'H':
                    showHit = !showHit;
                    break;
                case 'f':
                case 'F':
                    showWfm = !showWfm;
                    break;
                case 'd':
                case 'D':
                    showTDD = !showTDD;
                    break;

                case 'i':
                case 'I':
                    {
                        double seconds;
                        readTimeOfTest(&seconds);
                        _tprintf("TOT = %12.7f\n", seconds);
                    }
                    break;

                case '1':
                    {
                        double v = readForcedParametric(1);
                        _tprintf("Forced P1 = %lf\n", v);

                    }
                    break;

                default:
                    break;
                }
            }

            poll();

            // See if the DLL has a message for us
            if ( !getMessage(msg, 32767) )
            {
                dispMsg(msg, showHit, showWfm, showTDD);
                if ( waitForEnd == 1 )  // check if a stop command was issued
                {
                    if ( msg[2] == ID_STOP )    // see if this is the stop reponse,
                        done = 1;               //  if so we are done
                }
            }
            else    // no message
            {
                if ( waitForEnd == 2 )  // check if we need to abort
                {
                    // fabricate the abort message, display it and write it to the data file
                    static unsigned char abortMsg[] = { 7,0,15,0xff,0xff,0xff,0xff,0xff,0x7f };
                    dispMsg(abortMsg, showHit, showWfm, showTDD);
                    done = 1;
                }
            }

            // The function call to start an AST test takes astDone as a parameter.
            // When the AST is finished, the variable is set to non zero.
            if ( astDone != 0 )
            {
                 _tprintf("AST DONE\n");
                 astDone = 0;
            }

        } while ( !done );

        // leave the console display up until the user hits 'q'
        // anything else will start acquisition again
        _tprintf("\nPress Q to Exit");
    
        if ( _getch() == 'q' )
           running = 0;
        
        _tprintf("\n");
    }

    // Have to close the boards(s) when we are done
	printf("Exiting.\n");
    closePCI2();

    // Clean up
    if ( b1 != NULL )
        delete [] b1;

    if ( b2 != NULL )
        delete [] b2;

    CloseHandle(hStartStreamEvent);
    CloseHandle(hStopStreamEvent);
    CloseHandle(hExitThread);
    CloseHandle(shellH);

    return(1);
}
//***************************************************************
//
//  setupPCI2
//
//  Setup the PCI2 hardware to collect the data we want.
//
//***************************************************************

static void setupPCI2(int nChannels, int wfXfer)
{
    // Setup operating parameters for each channel.  Refer to the
    // documentation for default settings.  If the default value is
    // ok, you do not need to explicitly set it.

    for ( int i=1; i<=nChannels; i++ )
    {
        // Enable AE channel
        setChannel(i, ENABLE);
        // or use
        //setChannel(i,DISABLE);
        // to dsiable a channel
    
        // Set the sample rate in Ksamples/s, 1000 = 1Msample/s.
        setSampleRate(i, 500);

        // Set the threshold type, 0=fixed, 1=floating.
        setChannelThresholdType(i, 0);
   		
        // Deadband used for floating threshold only, specified in dB
		setChannelFloatingThresholdDeadband(i, 10);

        // Set the threshold, in dB for the channel
        setChannelThreshold(i, 60);

        // 0 Gain, could also be 6
        setChannelGain(i, 0); 

        // Set hit definition time in us, 2us steps
        setChannelHDT(i, 800); 
    
        // Set the peak defintion time in us, 1us steps
        setChannelPDT(i, 200); 

        // Set the hit lockout time in us, 2us steps
        setChannelHLT(i, 1000);   

        // Limit a hit to no longer than 10ms duration
        setChannelMaxDuration(i, 10);

        // Waveform length (in kSamples, 1kSample=1024 samples)
        setWaveformLength(i, 1);
    
        // Set the pre-trigger length in samples 
        setWaveformPreTrigger(i, 100);  

        // Set lowpass and highpass filter setting 
        setAnalogFilter(i, LOWPASS_INDEX, HIGHPASS_INDEX); 

		// Set preamplifier gain
		setPreAmpGain(i, 40);
    }

    // Generate a Time Driven message every 1 second
    setTimeDrivenRate(1000);    // specified in milliseconds

    // Set the time constant for RMS and ASL in milliseconds
    setRMS_ASL_TimeConstant(500);

    // We want Parametrics 1 & 2 data reported in the TD messages
    setTimeDrivenParametric(PARAM1, ENABLE);
    setTimeDrivenParametric(PARAM2, ENABLE);
    
    // If you want the cycle counter reported in the TD message, enable this
    setTimeDrivenParametric(CYCLES, DISABLE);

    // no gain on parametric 1
    setParametricGain(PARAM1, 0); 
    
    // no filter on either parametric
    setParametricFilter(PARAM1, 0);
    setParametricFilter(PARAM2, 0);

    // We want RMS and ASL for each enabled AE channel
    // reported in the TD messages
    setTimeDrivenFeature(RMS, ENABLE);
    setTimeDrivenFeature(ASL, ENABLE);

    // Hit features we want in the hit messages
    setHitFeature(AMPLITUDE, ENABLE);
    setHitFeature(ENERGY, ENABLE);
    setHitFeature(COUNTS, ENABLE);
    setHitFeature(DURATION, ENABLE);

    // Put parametrics 1 and 2 in the hit data set
    // also do the cycle counter
    setHitParametric(PARAM1, ENABLE);
    setHitParametric(PARAM2, ENABLE);
    setHitParametric(CYCLES, ENABLE);

    // Use parametric 1 as cycle counter input
    // 3V threshold and filter enabled
    setCycleCounterSource(PARAM1);
    setCycleCounterThreshold(3000);
    setCycleCounterFilter(ENABLE);

    // Enable waveform collection, start with waveform transfer enabled
    //setWaveformCollection(ENABLE);
    setWaveformTransfer(wfXfer);
}

//***************************************************************
//
//  dispMsg
//  Format and display on the console a message from the DLL 
//
//  msg - buffer containing the message to be displayed 
//  showHit
//
//
//***************************************************************

static void dispMsg(byte *msg, short showHit, short showWfm, short showTDD)
{
    unsigned short i, length = *(unsigned short *)msg;
    byte *p = msg;
    byte id;
    float v;
    short gotOne;

    switch ( msg[2] )
    {

    case 10:    // message id for AST hit
        if ( !showHit )
            return;

        _tprintf("AST ");
        
        // fall through to show the hit data from the AST

    case 1:     // AE hit message
        if ( !showHit )
            return;

        _tprintf("ID=%3u", msg[2]);
        
        // For demonstration only, don't test for all parameters,
        // just the ones that are enabled. This code is inefficient brute force
        if ( getHitDataSetValue(p, CHANNEL, &v) == GOOD )
            _tprintf(" CH=%3.0f", v);

        if ( getHitDataSetValue(p, TIME, &v) == GOOD )
            _tprintf(" TOT=%5.5f", v);

        if ( getHitDataSetValue(p, PARAM1, &v) == GOOD )
            _tprintf(" P1=%5.4f", v);
        if ( getHitDataSetValue(p, PARAM2, &v) == GOOD )
            _tprintf(" P2=%5.4f", v);
        if ( getHitDataSetValue(p, PARAM3, &v) == GOOD )
            _tprintf(" P3=%5.4f", v);
        if ( getHitDataSetValue(p, PARAM4, &v) == GOOD )
            _tprintf(" P4=%5.4f", v);
        if ( getHitDataSetValue(p, CYCLES, &v) == GOOD )
            _tprintf(" CC=%5.0f", v);

        if ( getHitDataSetValue(p, RISETIME, &v) == GOOD )
            _tprintf(" RT=%5.0f", v);
        if ( getHitDataSetValue(p, COUNTS, &v) == GOOD )
            _tprintf(" CNT= %5.0f", v);
        if ( getHitDataSetValue(p, COUNTS_TO_PEAK, &v) == GOOD )
            _tprintf(" CNP= %5.0f", v);
        if ( getHitDataSetValue(p, ENERGY, &v) == GOOD )
            _tprintf(" EN=%5.0f", v);
        if ( getHitDataSetValue(p, DURATION, &v) == GOOD )
            _tprintf(" DUR=%7.0f", v);
        if ( getHitDataSetValue(p, AVGFREQ, &v) == GOOD )
            _tprintf(" AFQ= %5.0f", v);
        if ( getHitDataSetValue(p, REV_FREQ, &v) == GOOD )
            _tprintf(" RFQ= %5.0f", v);
        if ( getHitDataSetValue(p, INIT_FREQ, &v) == GOOD )
            _tprintf(" IFQ= %5.0f", v);
        if ( getHitDataSetValue(p, SIG_STRENGTH, &v) == GOOD )
            _tprintf(" SS=%5.0f", v);
        if ( getHitDataSetValue(p, ABS_ENERGY, &v) == GOOD )
            _tprintf(" ABE=%5.0f", v);
        if ( getHitDataSetValue(p, ASL, &v) == GOOD )
            _tprintf(" ASL=%3.0f", v);
        if ( getHitDataSetValue(p, RMS, &v) == GOOD )
            _tprintf(" RMS=%5.5f", v);
        if ( getHitDataSetValue(p, AMPLITUDE, &v) == GOOD )
            _tprintf(" AMP=%3.0f", v);
        if ( getHitDataSetValue(p, FREQPEAK, &v) == GOOD )
            _tprintf(" PKFR=%4.0f", v);
        if ( getHitDataSetValue(p, FREQCENT, &v) == GOOD )
            _tprintf(" CNFR=%4.0f", v);
        if ( getHitDataSetValue(p, FREQPP1, &v) == GOOD )
            _tprintf(" FPP1=%4.0f", v);
        if ( getHitDataSetValue(p, FREQPP2, &v) == GOOD )
            _tprintf(" FPP2=%4.0f", v);
        if ( getHitDataSetValue(p, FREQPP3, &v) == GOOD )
            _tprintf(" FPP3=%4.0f", v);
        if ( getHitDataSetValue(p, FREQPP4, &v) == GOOD )
            _tprintf(" FPP4=%4.0f", v);

        _tprintf("\n");
        break;

    case 2:     // Time driven data message
        if ( !showTDD )
            return;

        _tprintf("ID=%3u", msg[2]);

        // For demonstration only, don't test for all parameters,
        // just the ones that are enabled. This code is inefficient brute force
        if ( getTddDataSetValue(p, 0, TIME, &v) == GOOD )
            _tprintf(" TOT=%5.5f", v);
        if ( getTddDataSetValue(p, 0, PARAM1, &v) == GOOD )
            _tprintf(" P1=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM2, &v) == GOOD )
            _tprintf(" P2=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM3, &v) == GOOD )
            _tprintf(" P3=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM4, &v) == GOOD )
            _tprintf(" P4=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM5, &v) == GOOD )
            _tprintf(" P5=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM6, &v) == GOOD )
            _tprintf(" P6=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM7, &v) == GOOD )
            _tprintf(" P7=%5.4f", v);
        if ( getTddDataSetValue(p, 0, PARAM8, &v) == GOOD )
            _tprintf(" P8=%5.4f", v);
        if ( getTddDataSetValue(p, 0, CYCLES, &v) == GOOD )
            _tprintf(" CC=%5.0f", v);

        _tprintf("\n");

        for ( i=1; i<=number_of_channels; i++ ) {
            gotOne = 0;
            if ( getTddDataSetValue(p, i, RMS, &v) == GOOD ) {               
                if( gotOne == 0 )
                {
                   _tprintf("Ch=%d", i);
                }
                
                _tprintf(" RMS=%5.4f", v);
                gotOne = 1;
            }
            if ( getTddDataSetValue(p, i, ASL, &v) == GOOD ) {
                if( gotOne == 0 )
                {
                   _tprintf("Ch=%d ", i);
                }
                _tprintf(" ASL=%5.4f", v);
                gotOne = 1;
            }
            if ( getTddDataSetValue(p, i, THRESHOLD, &v) == GOOD ) {
                if( gotOne == 0 )
                {
                   _tprintf("Ch=%d ", i);
                }
                _tprintf(" THR=%5.4f", v);
                gotOne = 1;
            }
            if ( getTddDataSetValue(p, i, ABS_ENERGY, &v) == GOOD ) {
                if( gotOne == 0 )
                {
                   _tprintf("Ch=%d ", i);
                }
                _tprintf(" ABE=%5.4f", v);
                gotOne = 1;
            }
            if ( gotOne )
                _tprintf("\n");
        }

        break;

    case 173:       // waveform related
        
        // switch on the sub-id
        switch ( msg[3] ) {
        
        case 10:    //  Waveform generated during an AST
            if ( !showWfm )
                return;

            _tprintf("AST ID=%3u, SID=%3u", msg[2], msg[3]);

            getWaveformValue(p, CHANNEL, &v);
            _tprintf(" CH=%3.0f", v);
            getWaveformValue(p, TIME, &v);
            _tprintf(" TOT=%5.5f", v);
            _tprintf("\n");
            break;

        case 1:     // Waveform for an AE hit
        
            // Special processing for a forced hit waveform.
            // Generate partial powers, frequency centroid and peak frequency
            // from the waveform.
            if ( forced_hit )
            {
               forced_hit = 0;
           
               short percentage[4];
               unsigned short centroid; 
               unsigned short peak;

               // Hz = bin * sample rate / 1024
               // bin = (Hz * 1024) / sample rate
               int startSegment[4] = {  2, 26, 51, 101 };
               int endSegment[4]   = { 25, 50, 100, 150 };
               setFftFrequencySpan(2, 150, 4, startSegment, endSegment); 

               // calculated the FFT and partial powers           
               calculateWaveformMsgFftPartialPowers(msg);
               calculateCentroidAndPeak(1000);

               getCentroidAndPeak(&centroid, &peak);

               getFftPartialPowers(0, &percentage[0]);
               getFftPartialPowers(1, &percentage[1]);
               getFftPartialPowers(2, &percentage[2]);
               getFftPartialPowers(3, &percentage[3]);

               _tprintf("Forced Waveform CentF:%d, PeakF:%d, PP1:%d, PP2:%d, PP3:%d, PP4:%d \r\n",
                      (int)centroid,
                      (int)peak,
                      (int)percentage[0], 
                      (int)percentage[1], 
                      (int)percentage[2],
                      (int)percentage[3]); 

               break;
            }

            if ( !showWfm )
                return;

            _tprintf("ID=%3u, SID=%3u", msg[2], msg[3]);

            getWaveformValue(p, CHANNEL, &v);
            _tprintf(" CH=%3.0f", v);
            getWaveformValue(p, TIME, &v);
            _tprintf(" TOT=%5.5f", v);

            _tprintf("\n");
            break;

        default:
            break;
        }           // end sub-id switch statement
        
        break;

    default:
        _tprintf("ID %3d", id=msg[2]);
        
        // Messages with time of test such as pause, resume, timemarks
        if ( getTimeOfCommand(p, &v) == GOOD )
            _tprintf(" TOT=%5.5f", v);

        _tprintf("\n");
        break;
    }       // end id switch statement

}



DWORD WINAPI streamThread(LPVOID arg)
{
    threadData &td = *(threadData *)arg;
    HANDLE hEvents[3];

    hEvents[0] = td.exitThread;
    hEvents[1] = td.startStream;
    hEvents[2] = td.stopStream;

    bool done = false;  
    while (!done)
    {
        // Wait for main thread to tell us to go.
        DWORD res = WaitForMultipleObjects(3, hEvents, FALSE, INFINITE) - WAIT_OBJECT_0;
        
        if ( res == 0 )
        {
            done = true;
        }
        else if ( res == 1 )
        {
            printf("Streaming Started\n");
            // Reset the event so we don't get multiple notifications
            ::ResetEvent(hEvents[1]);
        }
        else if ( res == 2 )
        {
            printf("Streaming Stopped\n");
            // Write the data for channel 1 out to a file.
            // The data is not scaled. It is signed 16 bit
            // data.
            // For 0dB gain, Volts = (data * 10) / 32768.0
            FILE *fp = fopen("STREAM.TXT", "wt");
            if ( fp != NULL )
            {
                for ( unsigned long i=0; i<td.channelDataLength[0]; i++ )
                    fprintf(fp, "%f\n", td.channelData[0][i]);
                fclose(fp);
            }

            // Reset the event so we don't get multiple notifications
            ::ResetEvent(hEvents[2]);
            
            // Done processing data, allow system to arm for next data collection
            rearmStreaming();
        }
    }

    return(0);
}


