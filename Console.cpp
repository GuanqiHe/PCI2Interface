//***************************************************************
//
//  Console.cpp
//  
//  Simple program to demonstrate using the pacpci2.dll in a 
//  program.
//  Compile this source file and link with PACPCI2.LIB
//  Copy the resulting program to a folder with PACPCI2.DLL,
//  and PCIAPI.DLL.
//  Run the program (requires a PAC PCI-2 board with the
//  Windows Drivers for the board installed on the
//  computer on which the program is run).
//                                                                                          
//***************************************************************

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

#include "pacp2lv.h"
#include "Pacpci2.h"

static byte msg[32767];

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

// For turning things on and off
#define ENABLE  1
#define DISABLE 0

// Lowpass and highpass filter selection settings
#define HIGHPASS_INDEX 0   // 0 = 1kHz  1 =  20kHz, 2 = 100kHz, 3 = 200kHz
#define LOWPASS_INDEX  0   // 0 = 3MHz, 1 = 100kHz, 2 = 200kHz, 3 = 400kHz, 4 = 1MHz, 5 = 2MHz

static int number_of_channels = 0; 
static int forced_hit = 0; 

// AEwin compatible DTA file output
#define MAKE_A_DTA_FILE 0       // set this to 1 to create a data file
#define DTA_FILE_NAME   "C:\\CONSOLE.DTA"   // this is where the data is saved
#define MAKE_A_WFS_FILE 0       // set this to 1 to do waveform streaming
    
//***************************************************************
//
//  Program entry point
//
//***************************************************************

int _tmain()
{
    unsigned short key;
    int ecode;
    int i; 

    // Must first open the board.  If it fails then the support files
    // listed in the header at the top of this file could be missing.
    if ( key = openPCI2() )
    {
        _tprintf(_T("Could not open PCI2 (%d) ... Hit a key to exit.\n"), key);
        if ( !_getch() ) _getch();
        return(0);
    }

    // Check how many parametric channels are available
    // display the result
    for ( i=1; i<=8; i++ )
    {      
        if ( !checkChannelParametricPresent(i) )
            break;

        number_of_channels = i;             
    }
    _tprintf(_T("#Parametrics = %d\n"), number_of_channels);

    // determine the number of channels avaliable.
    // Checking for consecutive channel numbers
    number_of_channels = 0;
    for ( i=1; i<=MAX_AE_CHANNELS; i++ )
    {      
        if ( !checkChannelHardwarePresent(i) )
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


    short running = 1;
    int astDone = 0;

    while ( running )
    {
        short done = 0;
        short waitForEnd = 0;
        // initial waveform transfer state
        short wfXfer = ENABLE;
        
        // Flags to control what messages go to the display
        short showTDD = 1;
        short showHit = 1;
        short showWfm = 1;

        // Setup the boards for our acquisition test
        setupPCI2(number_of_channels, wfXfer);
	
        // Waveform Streaming
#if MAKE_A_WFS_FILE

	    setWaveformStreamingMode(STREAMTRIG_TIMED);

	    setWaveformStreamingPeriod(10);	// 10sec between streams

	    setWaveformStreamingFilePrefix("EXAMPLE");	// first part of filename, second part is time/date

	    setWaveformStreamingChannel(1, TRUE);		// stream on channel 1
	    setWaveformStreamingChannel(2, TRUE);		// stream on channel 2

	    setWaveformStreamingLength(1, 2, 5);		// channel 1, 2ksample pretrigger, 5k total length
	    setWaveformStreamingLength(2, 2, 5);		// channel 2, 2ksample pretrigger, 5k total length

	    enableWaveformStreaming(TRUE);
#endif
        // Open a data file.
        // Write the hardware setup to it so that AEwin can replay the data file.
        FILE *fp = NULL;

#if MAKE_A_DTA_FILE

        fp = fopen(DTA_FILE_NAME, "wb");
        if ( fp != NULL )
        {
            static unsigned char clrclk[] = { 1,0,11 };
            unsigned char *setupBuffer;
            unsigned short setupBufferLength;
            
            // Find out how many bytes the setup messages are
            setupBufferLength = copySetupMessagesToBuffer(NULL);
            
            if ( setupBufferLength > 0 )
            {
                setupBuffer = new unsigned char[setupBufferLength];
                if ( setupBuffer != NULL )
                {
                    // Get the setup message data, write it to the file
                    setupBufferLength = copySetupMessagesToBuffer(setupBuffer);
                    fwrite(setupBuffer, setupBufferLength, 1, fp);
                    delete setupBuffer;

                    // A message 11 (clear clock) needs to go after the setup
                    // and before the start test messages
                    fwrite(clrclk, sizeof(clrclk), 1, fp);
                }
            }
            else
            {
                // Close up if an error occurred, file would be no good
                fclose(fp);
                fp = NULL;
            }
        }
#endif

        // Start collecting data
        if ( ecode = startTest() )
        {
            _tprintf("Could not Start Test (error %d).. Hit a key.\n", ecode);
            if ( !_getch() ) _getch();
            closePCI2();
            if ( fp != NULL )
                fclose(fp);
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
                if ( fp != NULL )
                    fwrite(msg, *(unsigned short *)msg+2, 1, fp);
                dispMsg(msg, showHit, showWfm, showTDD);
                if ( waitForEnd == 1 )  // check if a stop command was issued
                {
                    if ( msg[2] == 129 )    // see if this is the stop reponse,
                        done = 1;           //  if so we are done
                }
            }
            else    // no message
            {
                if ( waitForEnd == 2 )  // check if we need to abort
                {
                    // fabricate the abort message, display it and write it to the data file
                    static unsigned char abortMsg[] = { 7,0,15,0xff,0xff,0xff,0xff,0xff,0x7f };
                    dispMsg(abortMsg, showHit, showWfm, showTDD);
                    if ( fp != NULL )
                        fwrite(abortMsg, *(unsigned short *)abortMsg+2, 1, fp);
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

        // If we were writing to a data file, close the file
        if ( fp != NULL )
            fclose(fp);

        // leave the console display up until the user hits 'q'
        // anything else will start acquisition again
        _tprintf("\nPress Q to Exit");
    
        if ( _getch() == 'q' )
           running = 0;
        
        _tprintf("\n");
    }

    // Have to close the boards(s) when we are done
    // to release resources claimed by the dll
    closePCI2();

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
    for ( int i=1; i<=nChannels; i++ )
    {
        // Enable AE channel
        setChannel(i, ENABLE);
        // or use
        //setChannel(i,DISABLE);
        // to dsiable a channel
    
        // Set the sample rate in Ksamples/s, 1000 = 1Msample/s.
        setSampleRate(i, 1000);

        // Set the threshold type, 0=fixed, 1=floating.
        setChannelThresholdType(i, 0);

        // Deadband used for floating threshold only, specified in dB
		setChannelFloatingThresholdDeadband(i, 10);

        // Set the threshold, in dB for the channel
        setChannelThreshold(i, 45);

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
    setHitFeature(AVGFREQ, ENABLE);
    setHitFeature(SIG_STRENGTH, ENABLE);
    setHitFeature(ABS_ENERGY, ENABLE);
    setHitFeature(PART_POWERS, ENABLE);     // partial powers enabled

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

    // Set the segements for the partial powers
   // Hz = bin * sample rate / 1024
   // bin = (Hz * 1024) / sample rate
    int startSegment[4] = {  10, 111, 211, 311 };
    int endSegment[4]   = { 110, 210, 310, 410 };
    setHitFftFrequencySpan(10, 410, 4, startSegment, endSegment); 

    // Add frequency centroid and peak frequency to the AE data set
    setHitFeature(FREQCENT, ENABLE);
    setHitFeature(FREQPEAK, ENABLE);

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

