// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientmodel.h"

#include "guiconstants.h"
#include "peertablemodel.h"

#include "alert.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "net.h"
#include "ui_interface.h"
#include "util.h"

#include <stdint.h>

#include <QDateTime>
#include <QDebug>
#include <QTimer>

static const int64_t nClientStartupTime = GetTime();

ClientModel::ClientModel(OptionsModel* optionsModel, QObject* parent) : QObject(parent),
                                                                        optionsModel(optionsModel),
                                                                        peerTableModel(0),
                                                                        cachedNumBlocks(0),
                                                                        cachedMasternodeCountString(""),
                                                                        cachedReindexing(0), cachedImporting(0),
                                                                        numBlocksAtStartup(-1), pollTimer(0)
{
    peerTableModel = new PeerTableModel(this);
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    pollMnTimer = new QTimer(this);
    connect(pollMnTimer, SIGNAL(timeout()), this, SLOT(updateMnTimer()));
    // no need to update as frequent as data for balances/txes/blocks
    pollMnTimer->start(MODEL_UPDATE_DELAY * 4);

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{
    unsubscribeFromCoreSignals();
}

int ClientModel::getNumConnections(unsigned int flags) const
{
    LOCK(cs_vNodes);
    if (flags == CONNECTIONS_ALL) // Shortcut if we want total
        return vNodes.size();

    int nNum = 0;
    BOOST_FOREACH (CNode* pnode, vNodes)
        if (flags & (pnode->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT))
            nNum++;

    return nNum;
}

QString ClientModel::getMasternodeCountString() const
{
    int ipv4 = 0, ipv6 = 0, onion = 0;
    mnodeman.CountNetworks(ActiveProtocol(), ipv4, ipv6, onion);
    int nUnknown = mnodeman.size() - ipv4 - ipv6 - onion;
    if(nUnknown < 0) nUnknown = 0;
    return tr("Total: %1 (IPv4: %2 / IPv6: %3 / Tor: %4 / Unknown: %5)").arg(QString::number((int)mnodeman.size())).arg(QString::number((int)ipv4)).arg(QString::number((int)ipv6)).arg(QString::number((int)onion)).arg(QString::number((int)nUnknown));
}

int ClientModel::getNumBlocks() const
{
    LOCK(cs_main);
    return chainActive.Height();
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

quint64 ClientModel::getTotalBytesRecv() const
{
    return CNode::GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    return CNode::GetTotalBytesSent();
}

QDateTime ClientModel::getLastBlockDate() const
{
    LOCK(cs_main);
    if (chainActive.Tip())
        return QDateTime::fromTime_t(chainActive.Tip()->GetBlockTime());
    else
        return QDateTime::fromTime_t(Params().GenesisBlock().GetBlockTime()); // Genesis block's time of current network
}

double ClientModel::getVerificationProgress() const
{
    LOCK(cs_main);
    return Checkpoints::GuessVerificationProgress(chainActive.Tip());
}

void ClientModel::updateTimer()
{
    // Get required lock upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;
    // Some quantities (such as number of blocks) change so fast that we don't want to be notified for each change.
    // Periodically check and update with a timer.
    int newNumBlocks = getNumBlocks();

    static int prevAttempt = -1;
    static int prevAssets = -1;

    // check for changed number of blocks we have, number of blocks peers claim to have, reindexing state and importing state
    if (cachedNumBlocks != newNumBlocks ||
        cachedReindexing != fReindex || cachedImporting != fImporting ||
        masternodeSync.RequestedMasternodeAttempt != prevAttempt || masternodeSync.RequestedMasternodeAssets != prevAssets) {
        cachedNumBlocks = newNumBlocks;
        cachedReindexing = fReindex;
        cachedImporting = fImporting;
        prevAttempt = masternodeSync.RequestedMasternodeAttempt;
        prevAssets = masternodeSync.RequestedMasternodeAssets;

        emit numBlocksChanged(newNumBlocks);
    }

    emit bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateMnTimer()
{
    // Get required lock upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;
    QString newMasternodeCountString = getMasternodeCountString();

    if (cachedMasternodeCountString != newMasternodeCountString) {
        cachedMasternodeCountString = newMasternodeCountString;

        emit strMasternodesChanged(cachedMasternodeCountString);
    }
}

void ClientModel::updateNumConnections(int numConnections)
{
    emit numConnectionsChanged(numConnections);
}

void ClientModel::updateAlert(const QString& hash, int status)
{
    // Show error message notification for new alert
    if (status == CT_NEW) {
        uint256 hash_256;
        hash_256.SetHex(hash.toStdString());
        CAlert alert = CAlert::getAlertByHash(hash_256);
        if (!alert.IsNull()) {
            emit message(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), CClientUIInterface::ICON_ERROR);
        }
    }

    emit alertsChanged(getStatusBarWarnings());
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

enum BlockSource ClientModel::getBlockSource() const
{
    if (fReindex)
        return BLOCK_SOURCE_REINDEX;
    else if (fImporting)
        return BLOCK_SOURCE_DISK;
    else if (getNumConnections() > 0)
        return BLOCK_SOURCE_NETWORK;

    return BLOCK_SOURCE_NONE;
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("statusbar"));
}

OptionsModel* ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel* ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatBuildDate() const
{
    return QString::fromStdString(CLIENT_DATE);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::clientName() const
{
    return QString::fromStdString(CLIENT_NAME_STR);
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

// Handlers for core signals
static void ShowProgress(ClientModel* clientmodel, const std::string& title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(title)),
        Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel* clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged : " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
        Q_ARG(int, newNumConnections));
}

static void NotifyAlertChanged(ClientModel* clientmodel, const uint256& hash, ChangeType status)
{
    qDebug() << "NotifyAlertChanged : " + QString::fromStdString(hash.GetHex()) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(hash.GetHex())),
        Q_ARG(int, status));
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this, _1, _2));
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this, _1, _2));
}
