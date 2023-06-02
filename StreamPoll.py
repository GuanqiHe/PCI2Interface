from PacPci2 import *
from PACP2LV import *

MAXMSGLEN = 32767
msg = (byte*MAXMSGLEN)(*[0 for i in range(MAXMSGLEN)])

MAX_AE_CHANNELS = 2

ENABLE = 1
DISABLE = 0

HIGHPASS_INDEX = 2
LOWPASS_INDEX = 0

number_of_channels = 0
forced_hit = 0


ID_PAUSE = 130
ID_ABORT = 15
ID_RESUME = 128
ID_STOP = 129

TRUE = True
FALSE = False


def mainFunc():
    done = 0
    key = openPCI2()
    print(key)
    if key:
        print("Could not open PCI2 ({}) ... Hit a key to exit.\n".format(key))
        return 0

    for i in range(1, MAX_AE_CHANNELS+1):
        if not checkChannelHardwarePresent(c_short(i)):
            break

        number_of_channels = i

    if number_of_channels == 0:
        print("Could not find any PCI2 channels ... Hit a key to exit.\n")
        return(0)

    wfXfer = ENABLE

    setupPCI2(number_of_channels, wfXfer)

    setWaveformStreamingMode(STREAMTRIG_TIMED)

    setWaveformStreamingPeriod(5)

    setWaveformStreamingFilePrefix("EXAMPLE")

    enableWFSOutput(0)

    setWaveformStreamingChannel(1, TRUE)

    setWaveformStreamingChannel(2, TRUE)

    wfStreamLength_k = 5

    setWaveformStreamingLength(1, 2, wfStreamLength_k)

    setWaveformStreamingLength(2, 2, wfStreamLength_k)
    
    enableWaveformStreaming(TRUE)

    enablePolling(1)

    b1Len = getRequiredSampleBufferLen(1)
    b2Len = getRequiredSampleBufferLen(2)

    b1Array = (c_float*b1Len)()
    b1 = cast(b1Array, POINTER(c_float))

    b2Array = (c_float*b2Len)()
    b2 = cast(b2Array, POINTER(c_float))

    setStreamingBuffer(1, b1, b1Len, 1)
    setStreamingBuffer(2, b2, b1Len, 1)

    validateSetup()

    running  = 1
    asDone = 0


def setupPCI2(nChannels, wfXfer):

    for i in range(1, nChannels+1):
        setChannel(i, ENABLE)
        setSampleRate(i, 500)

        setChannelThresholdType(i, 0)
   		
        setChannelFloatingThresholdDeadband(i, 10)

        setChannelThreshold(i, 60)

        setChannelGain(i, 0)

        setChannelHDT(i, 800)
    
        setChannelPDT(i, 200)

        setChannelHLT(i, 1000)

        setChannelMaxDuration(i, 10)

        setWaveformLength(i, 1)
    
        setWaveformPreTrigger(i, 100)

        setAnalogFilter(i, LOWPASS_INDEX, HIGHPASS_INDEX)

        setPreAmpGain(i, 40)
    
    setTimeDrivenRate(1000)

    setRMS_ASL_TimeConstant(500)

    setTimeDrivenParametric(PARAM1, ENABLE)
    setTimeDrivenParametric(PARAM2, ENABLE)
    
    setTimeDrivenParametric(CYCLES, DISABLE)

    setParametricGain(PARAM1, 0)
    
    setParametricFilter(PARAM1, 0)
    setParametricFilter(PARAM2, 0)

    setTimeDrivenFeature(RMS, ENABLE)
    setTimeDrivenFeature(ASL, ENABLE)

    setHitFeature(AMPLITUDE, ENABLE)
    setHitFeature(ENERGY, ENABLE)
    setHitFeature(COUNTS, ENABLE)
    setHitFeature(DURATION, ENABLE)

    setHitParametric(PARAM1, ENABLE)
    setHitParametric(PARAM2, ENABLE)
    setHitParametric(CYCLES, ENABLE)

    setCycleCounterSource(PARAM1)
    setCycleCounterThreshold(3000)
    setCycleCounterFilter(ENABLE)

    setWaveformTransfer(wfXfer)










if __name__ == '__main__':

    mainFunc()
