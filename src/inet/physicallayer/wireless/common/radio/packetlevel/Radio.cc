//
// Copyright (C) 2013 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "inet/physicallayer/wireless/common/radio/packetlevel/Radio.h"

#include "inet/common/LayeredProtocolBase.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/Simsignals.h"
#include "inet/common/stlutils.h"
#include "inet/physicallayer/wireless/common/contract/packetlevel/SignalTag_m.h"
#include "inet/physicallayer/wireless/common/medium/RadioMedium.h"

#ifdef NS3_VALIDATION
#include "inet/linklayer/ieee80211/mac/Ieee80211Frame_m.h"
#endif

namespace inet {
namespace physicallayer {

Define_Module(Radio);

Radio::~Radio()
{
    // NOTE: can't use the medium module here, because it may have been already deleted
    cModule *medium = getSimulation()->getModule(mediumModuleId);
    if (medium != nullptr)
        check_and_cast<IRadioMedium *>(medium)->removeRadio(this);
    cancelAndDelete(transmissionTimer);
    cancelAndDelete(switchTimer);
    for (auto timer : allReceptionTimers)
        cancelAndDelete(timer);
    allReceptionTimers.clear();
}

void Radio::initialize(int stage)
{
    // Register before the base class starts the lifecycle. Starting in a
    // receiver mode already listens on the medium, and the medium must know
    // the radio by then. Same ordering as #1220.
    if (stage == INITSTAGE_PHYSICAL_LAYER)
        medium->addRadio(this);
    PhysicalLayerBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        switchTimer = new cMessage("switchTimer");
        transmissionTimer = new cMessage("transmissionTimer");
        antenna = check_and_cast<IAntenna *>(getSubmodule("antenna"));
        transmitter = check_and_cast<ITransmitter *>(getSubmodule("transmitter"));
        receiver = check_and_cast<IReceiver *>(getSubmodule("receiver"));
        medium.reference(this, "radioMediumModule", true);
        mediumModuleId = check_and_cast<cModule *>(medium.get())->getId();
        upperLayerIn = gate("upperLayerIn");
        upperLayerOut = gate("upperLayerOut");
        radioIn = gate("radioIn");
        radioIn->setDeliverImmediately(true);
        sendRawBytes = par("sendRawBytes");
        separateTransmissionParts = par("separateTransmissionParts");
        separateReceptionParts = par("separateReceptionParts");
        WATCH(mediumModuleId);
        WATCH_EXPR("radioMode", opp_removestart(cEnum::getNameForValue(radioMode), "RADIO_MODE_"));
        WATCH_EXPR("nextRadioMode", opp_removestart(cEnum::getNameForValue(nextRadioMode), "RADIO_MODE_"));
        WATCH_EXPR("previousRadioMode", opp_removestart(cEnum::getNameForValue(previousRadioMode), "RADIO_MODE_"));
        WATCH_EXPR("receptionState", opp_removestart(cEnum::getNameForValue(receptionState), "RECEPTION_STATE_"));
        WATCH_EXPR("transmissionState", opp_removestart(cEnum::getNameForValue(transmissionState), "TRANSMISSION_STATE_"));
        WATCH(receivedSignalPart);
        WATCH(transmittedSignalPart);
        WATCH(transmissionTimer);
        WATCH(receptionTimer);
        WATCH(switchTimer);
        WATCH(allReceptionTimers);
    }
    else if (stage == INITSTAGE_PHYSICAL_LAYER) {
        if (medium->getCommunicationCache()->getNumTransmissions() == 0 && isListeningPossible())
            throw cRuntimeError("Receiver is busy without any ongoing transmission, probably energy detection level is too low or background noise level is too high");
        // initialRadioMode is applied only from handleStartOperation(), which
        // runs when the node is up. A node that starts down skips that, so
        // the radio stays in RADIO_MODE_OFF.
        parseRadioModeSwitchingTimes();
    }
    else if (stage == INITSTAGE_LAST) {
        EV_INFO << "Initialized " << getCompleteStringRepresentation() << endl;
    }
}

void Radio::initializeRadioMode()
{
    const char *initialRadioMode = par("initialRadioMode");
    if (!strcmp(initialRadioMode, "off"))
        completeRadioModeSwitch(IRadio::RADIO_MODE_OFF);
    else if (!strcmp(initialRadioMode, "sleep"))
        completeRadioModeSwitch(IRadio::RADIO_MODE_SLEEP);
    else if (!strcmp(initialRadioMode, "receiver"))
        completeRadioModeSwitch(IRadio::RADIO_MODE_RECEIVER);
    else if (!strcmp(initialRadioMode, "transmitter"))
        completeRadioModeSwitch(IRadio::RADIO_MODE_TRANSMITTER);
    else if (!strcmp(initialRadioMode, "transceiver"))
        completeRadioModeSwitch(IRadio::RADIO_MODE_TRANSCEIVER);
    else
        throw cRuntimeError("Unknown initialRadioMode");
}
