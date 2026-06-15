//========================================================================
//
// Copyright (C) 2020 Matthieu Bruel <Matthieu.Bruel@gmail.com>
// Copyright (C) 2026 BakasuraRCE <bakasura@protonmail.ch>
// This file is a part of ngPostEx : https://github.com/BakasuraRCE/ngPostEx
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3..
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>
//
//========================================================================

#include "NzbCheck.h"
#include "NntpCheckCon.h"
#include "nntp/NntpServerParams.h"
#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QTime>

const QRegularExpression NzbCheck::sNntpArticleYencSubjectRegExp = QRegularExpression(sNntpArticleYencSubjectStrRegExp);

//! Regex to detect PAR2 files from the subject line (case insensitive)
//! Matches .par2 followed by optional closing quote, then anything, then yEnc (N/N)
static const QRegularExpression sPar2FileRegExp(
    "\\.par2\"?\\s.*yEnc\\s+\\(\\d+/\\d+\\)",
    QRegularExpression::CaseInsensitiveOption);

//! Regex to extract par2 volume block count from filename like .vol00+05.par2
static const QRegularExpression sPar2VolBlocksRegExp(
    "\\.vol\\d+\\+(\\d+)\\.par2",
    QRegularExpression::CaseInsensitiveOption);

void NzbCheck::onDisconnected(NntpCheckCon *con)
{
    _connections.remove(con);
    if (_connections.isEmpty())
    {
        if (_dispProgressBar)
        {
            disconnect(&_progressbarTimer, &QTimer::timeout, this, &NzbCheck::onRefreshprogressbarBar);
            onRefreshprogressbarBar();
            _cout << "\n" << MB_FLUSH;
        }

        if (_nbCheckedArticles == 0 && _nbTotalArticles > 0)
        {
            _cerr << tr("ERROR: check FAILED - no articles could be verified (0/%1). "
                        "All connections were refused or dropped. "
                        "Another program may be using all available connections on the server(s).").arg(
                         _nbTotalArticles) << "\n" << MB_FLUSH;
        }
        else if (_nbCheckedArticles < _nbTotalArticles)
        {
            _cerr << tr("ERROR: check INCOMPLETE - only %1/%2 articles were verified. "
                        "Some connections failed (possibly max connections reached on server). "
                        "Results are unreliable.").arg(
                         _nbCheckedArticles).arg(_nbTotalArticles) << "\n" << MB_FLUSH;
        }
        else if (!_quietMode)
        {
            qint64 duration = _timeStart.elapsed();
            _cout << tr("Nb Missing Article(s): %1/%2 (check done in %3 (%4 sec) using %5 connections on %6 server(s))").arg(
                         _nbMissingArticles).arg(
                         _nbTotalArticles).arg(
                         QTime::fromMSecsSinceStartOfDay(static_cast<int>(duration)).toString("hh:mm:ss.zzz")).arg(
                         std::round(1.*duration/1000)).arg(
                         _nbCons).arg(
                         nbCheckingServers()) << "\n" << MB_FLUSH;

            // Print recovery analysis after successful check
            printRecoveryAnalysis();
        }
        qApp->quit();
    }
}

void NzbCheck::onRefreshprogressbarBar()
{
    float progressbar = static_cast<float>(_nbCheckedArticles);
    progressbar /= _nbTotalArticles;

    _cout << "\r[";
    int pos = static_cast<int>(std::floor(progressbar * sprogressbarBarWidth));
    for (int i = 0; i < sprogressbarBarWidth; ++i) {
        if (i < pos) _cout << "=";
        else if (i == pos) _cout << ">";
        else _cout << " ";
    }
    _cout << "] " << int(progressbar * 100) << " %"
              << " (" << _nbCheckedArticles << " / " << _nbTotalArticles << ")"
              << tr(" missing: ") << _nbMissingArticles;
    _cout.flush();

    if (_nbCheckedArticles < _nbTotalArticles)
        _progressbarTimer.start(_refreshRate);
}

NzbCheck::NzbCheck():QObject(),
    _nzbPath(), _articles(),
    _cout(stdout), _cerr(stderr),
    _nbTotalArticles(0),  _nbMissingArticles(0), _nbCheckedArticles(0),
    _nntpServers(),
    _debug(0), _connections(),
    _dispProgressBar(false), _progressbarTimer(), _refreshRate(sDefaultRefreshRate),
    _quietMode(false),
    _nbMaxRetry(5), _socketTimeOut(30000),
    _nbPar2Articles(0), _nbDataArticles(0),
    _nbMissingPar2Articles(0), _nbMissingDataArticles(0),
    _par2Volumes(), _articleToVolume(),
    _par2BlockSize(716800), _articleSize(716800)
{}

NzbCheck::~NzbCheck()
{
    if (_dispProgressBar)
        _progressbarTimer.stop();
}

int NzbCheck::parseNzb()
{
    QFile file(_nzbPath);
    if (file.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QXmlStreamReader xmlReader(&file);
        while ( !xmlReader.atEnd() )
        {
            QXmlStreamReader::TokenType type = xmlReader.readNext();
            if (type == QXmlStreamReader::TokenType::StartElement
                    && xmlReader.name().compare(QLatin1String("file")) == 0)
            {
                QString subject = xmlReader.attributes().value("subject").toString();
                QRegularExpressionMatch match = sNntpArticleYencSubjectRegExp.match(subject);
                int nbArticles = 0, nbExpectedArticles = 0;
                if (match.hasMatch())
                    nbExpectedArticles = match.captured(1).toInt();

                // Determine if this file is a PAR2 file
                bool isPar2 = sPar2FileRegExp.match(subject).hasMatch();

                // If PAR2, create a volume entry for per-volume tracking
                int volIndex = -1;
                if (isPar2)
                {
                    Par2Volume vol;
                    vol.subject = subject;
                    vol.nbExpectedArticles = nbExpectedArticles;

                    // Extract par2 volume block count (e.g. .vol00+05.par2 = 5 blocks)
                    QRegularExpressionMatch volMatch = sPar2VolBlocksRegExp.match(subject);
                    if (volMatch.hasMatch())
                    {
                        vol.blocks = volMatch.captured(1).toInt();
                        vol.isVolume = true;
                    }
                    else
                    {
                        // Base .par2 file (no vol): 0 recovery blocks, just metadata
                        vol.blocks = 0;
                        vol.isVolume = false;
                    }

                    volIndex = _par2Volumes.size();
                    _par2Volumes.append(vol);
                }

                while ( !xmlReader.atEnd() )
                {
                    QXmlStreamReader::TokenType type = xmlReader.readNext();
                    if (type == QXmlStreamReader::TokenType::EndElement
                            && xmlReader.name().compare(QLatin1String("file")) == 0)
                    {
                        if (debugMode())
                            _cout << tr("The file '%1' has %2 articles in the nzb (expected: %3)%4").arg(
                                         subject).arg(nbArticles).arg(nbExpectedArticles).arg(
                                         isPar2 ? " [PAR2]" : "") << "\n" << MB_FLUSH;
                        if (nbArticles < nbExpectedArticles)
                        {
                            if (!_quietMode)
                                _cout << tr("- %1 missing Article(s) in nzb for '%2'").arg(
                                         nbExpectedArticles - nbArticles).arg(subject) << "\n" << MB_FLUSH;

                            int missingInNzb = nbExpectedArticles - nbArticles;
                            _nbMissingArticles += missingInNzb;
                            if (isPar2)
                            {
                                _nbMissingPar2Articles += missingInNzb;
                                _par2Volumes[volIndex].nbMissingArticles += missingInNzb;
                            }
                            else
                                _nbMissingDataArticles += missingInNzb;
                        }

                        break;
                    }
                    else if (type == QXmlStreamReader::TokenType::StartElement
                            && xmlReader.name().compare(QLatin1String("segment")) == 0)
                    {
                        ++nbArticles;
                        xmlReader.readNext();
                        QString articleId = QString("<%1>").arg(xmlReader.text().toString());
                        _articles.push(articleId);
                        if (isPar2)
                        {
                            _articleToVolume.insert(articleId, volIndex);
                            _par2Volumes[volIndex].articleIds.insert(articleId);
                            ++_nbPar2Articles;
                        }
                        else
                        {
                            ++_nbDataArticles;
                        }
                    }
                }
            }
        }

        if (xmlReader.hasError()) {
            _cerr << "parsing error: " << xmlReader.errorString()
                      << " at line: " << xmlReader.lineNumber() << "\n" << MB_FLUSH;
            return -2;
        }
        _nbTotalArticles = _articles.size();
        if (!_quietMode)
        {
            int totalBlocks = 0;
            int nbVolumes = 0;
            for (const Par2Volume &vol : _par2Volumes)
            {
                if (vol.isVolume)
                {
                    totalBlocks += vol.blocks;
                    ++nbVolumes;
                }
            }

            _cout << tr("%1 has %2 articles (%3 data + %4 par2 in %5 volume(s))").arg(
                         QFileInfo(_nzbPath).fileName()).arg(_nbTotalArticles).arg(
                         _nbDataArticles).arg(_nbPar2Articles).arg(nbVolumes) << "\n" << MB_FLUSH;
            if (totalBlocks > 0)
                _cout << tr("PAR2 total recovery blocks: %1 (across %2 volumes)").arg(
                             totalBlocks).arg(nbVolumes) << "\n" << MB_FLUSH;
            else if (_nbPar2Articles == 0)
                _cout << tr("WARNING: No PAR2 files found in NZB - no recovery possible") << "\n" << MB_FLUSH;
        }
        return _nbTotalArticles;
    }
    else
    {
        _cerr << tr("Error opening nzb file...") << "\n" << MB_FLUSH;
        return -1;
    }

}

void NzbCheck::checkPost()
{
    _timeStart.start();

    _nbCons = 0;
    for (NntpServerParams *srvParam : _nntpServers)
    {
        if (srvParam->nzbCheck)
            _nbCons += srvParam->nbCons;
    }

    _nbCons = std::min(_nbTotalArticles, _nbCons);

    int nb = 0;
    for (NntpServerParams *srvParam : _nntpServers)
    {
        if (srvParam->nzbCheck)
        {
            for (int i = 1 ; i <= srvParam->nbCons; ++i)
            {
                NntpCheckCon *con = new NntpCheckCon(this, i, *srvParam);
                connect(con, &NntpCheckCon::disconnected, this, &NzbCheck::onDisconnected, Qt::DirectConnection);
                emit con->startConnection();

                _connections.insert(con);

                if (++nb == _nbCons)
                    break;
            }
            if (nb == _nbCons)
                break;
        }
    }

    if (debugMode())
        _cout << tr("Using %1 Connections").arg(_nbCons) << "\n" << MB_FLUSH;

    if (_dispProgressBar)
    {
        connect(&_progressbarTimer, &QTimer::timeout, this, &NzbCheck::onRefreshprogressbarBar, Qt::DirectConnection);
        _progressbarTimer.start(_refreshRate);
    }
}

int NzbCheck::nbCheckingServers()
{
    int nb = 0;
    for (NntpServerParams *srvParam : _nntpServers)
    {
        if (srvParam->nzbCheck)
            ++nb;
    }
    return nb;
}

bool NzbCheck::isRecoverable() const
{
    if (_nbMissingDataArticles == 0)
        return true; // No missing data articles, nothing to recover

    if (_par2Volumes.isEmpty())
        return false; // No PAR2 at all

    // Must have metadata available (at least one intact PAR2 file — base or volume)
    if (!hasIntactPar2Metadata())
        return false;

    // Calculate effective blocks: sum of blocks from intact volumes only
    int effective = effectiveRecoveryBlocks();

    // Estimate how many PAR2 blocks are actually damaged
    // When par2BlockSize > articleSize, multiple lost articles may affect the same block
    int damagedBlocks = estimateDamagedBlocks();

    return damagedBlocks <= effective;
}

int NzbCheck::estimateDamagedBlocks() const
{
    // How many PAR2 data blocks are damaged by the missing articles?
    //
    // If par2BlockSize == articleSize: 1 missing article = 1 damaged block (worst case)
    // If par2BlockSize > articleSize: multiple articles fit in one block,
    //   so fewer blocks may be damaged than articles lost.
    //
    // Worst case (conservative): each missing article damages a different block.
    // Best case (optimistic): all missing articles are in the same block(s).
    //
    // We use a realistic estimate: ceil(missingArticles * articleSize / par2BlockSize)
    // This assumes missing articles are spread across blocks (not clustered).
    // The result is the maximum number of blocks that could be damaged.
    if (_par2BlockSize <= _articleSize)
        return _nbMissingDataArticles; // 1:1 ratio, each article = at least 1 block

    // par2BlockSize > articleSize: multiple articles per block
    // Total bytes lost ≈ missingArticles * articleSize
    // Blocks damaged ≈ ceil(bytes_lost / par2BlockSize)
    double bytesLost = static_cast<double>(_nbMissingDataArticles) * _articleSize;
    return static_cast<int>(std::ceil(bytesLost / _par2BlockSize));
}

int NzbCheck::effectiveRecoveryBlocks() const
{
    int blocks = 0;
    for (const Par2Volume &vol : _par2Volumes)
    {
        if (vol.isVolume && vol.isIntact())
            blocks += vol.blocks;
    }
    return blocks;
}

bool NzbCheck::hasIntactPar2Metadata() const
{
    // Metadata (File Description, Main Packet, etc.) is stored in the .par2 base
    // AND replicated in every .vol*.par2 file.
    // If at least ONE par2 file (base or volume) is fully intact, metadata is available.
    for (const Par2Volume &vol : _par2Volumes)
    {
        if (vol.isIntact())
            return true;
    }
    return false;
}

int NzbCheck::computeHealthScore() const
{
    // Health score: 0 (catastrophic) to 100 (perfect)
    //
    // Usenet-oriented: files degrade over time (retention), so future recovery
    // capacity (PAR2 blocks) is MORE important than current data state.
    //
    // Criteria:
    //   1. Data integrity (30 pts): based on % of data articles present
    //   2. PAR2 recovery capacity (45 pts): effective blocks vs total blocks available
    //      (this measures resilience — can we recover IF data gets damaged?)
    //   3. PAR2 metadata availability (10 pts): at least one intact par2 file
    //   4. Recovery potential (15 pts): if damage occurs, can we actually repair?
    //      Based on available recovery blocks, NOT on current damage state.

    int score = 0;

    // --- 1. Data integrity: 30 points ---
    if (_nbDataArticles == 0)
        score += 30;
    else
    {
        double dataIntegrity = 1.0 - (static_cast<double>(_nbMissingDataArticles) / _nbDataArticles);
        score += static_cast<int>(dataIntegrity * 30.0);
    }

    // --- 2. PAR2 recovery capacity: 45 points ---
    // Measures: what % of PAR2 blocks are still usable?
    // This is the most critical metric for Usenet: losing PAR2 blocks means
    // future data loss becomes unrecoverable.
    int totalBlocks = 0, intactBlocks = 0;
    if (_par2Volumes.isEmpty())
    {
        // No PAR2 at all — zero resilience
        score += 0;
    }
    else
    {
        for (const Par2Volume &vol : _par2Volumes)
        {
            if (vol.isVolume)
            {
                totalBlocks += vol.blocks;
                if (vol.isIntact())
                    intactBlocks += vol.blocks;
            }
        }
        if (totalBlocks == 0)
            score += 0;
        else
        {
            double blockHealth = static_cast<double>(intactBlocks) / totalBlocks;
            score += static_cast<int>(blockHealth * 45.0);
        }
    }

    // --- 3. PAR2 metadata availability: 10 points ---
    if (_par2Volumes.isEmpty())
        score += 0;
    else if (hasIntactPar2Metadata())
        score += 10;
    else
        score += 0;

    // --- 4. Recovery potential: 15 points ---
    // Reflects: "if data gets damaged, can I repair it?"
    // This is proportional to available recovery blocks regardless of current damage.
    // Having 0 effective blocks = 0 points even if data is currently intact,
    // because any future loss will be unrecoverable.
    if (_par2Volumes.isEmpty())
    {
        score += 0;
    }
    else if (totalBlocks == 0)
    {
        score += 0;
    }
    else
    {
        double recoveryPotential = static_cast<double>(intactBlocks) / totalBlocks;
        score += static_cast<int>(recoveryPotential * 15.0);
    }

    // --- Cap for unrecoverable state ---
    // If data is damaged AND recovery is impossible, the NZB is effectively dead.
    // The score should not exceed 25 regardless of how much data remains intact,
    // because the practical outcome is the same: the file cannot be reconstructed.
    if (_nbMissingDataArticles > 0 && !isRecoverable())
        score = qMin(score, 25);

    return qBound(0, score, 100);
}

void NzbCheck::printRecoveryAnalysis()
{
    _cout << "\n" << tr("=== Recovery Analysis ===") << "\n";
    _cout << tr("  Data articles: %1 (missing: %2)").arg(_nbDataArticles).arg(_nbMissingDataArticles) << "\n";
    _cout << tr("  PAR2 articles: %1 (missing: %2)").arg(_nbPar2Articles).arg(_nbMissingPar2Articles) << "\n";

    // --- Early return: no PAR2 files at all ---
    if (_par2Volumes.isEmpty())
    {
        _cout << tr("  PAR2 redundancy: NONE") << "\n";
        int health = computeHealthScore();
        if (_nbMissingDataArticles > 0)
        {
            _cout << tr("  Status: UNRECOVERABLE - No PAR2 files in NZB") << "\n";
            _cout << "\n" << tr("  Health: %1/100").arg(health) << "\n";
            _cout << tr("  >> Dead - unrecoverable, must be re-posted from source") << "\n" << MB_FLUSH;
        }
        else
        {
            _cout << tr("  Status: COMPLETE but UNPROTECTED - No PAR2 files in NZB") << "\n";
            _cout << "\n" << tr("  Health: %1/100").arg(health) << "\n";
            _cout << tr("  >> Critical - data intact but no PAR2 exists, no recovery or verification possible") << "\n" << MB_FLUSH;
        }
        return;
    }

    // --- PAR2 volume/block summary ---
    int totalBlocks = 0, intactBlocks = 0;
    int intactVolumes = 0, damagedVolumes = 0;
    for (const Par2Volume &vol : _par2Volumes)
    {
        if (vol.isVolume)
        {
            totalBlocks += vol.blocks;
            if (vol.isIntact())
            {
                intactBlocks += vol.blocks;
                ++intactVolumes;
            }
            else
                ++damagedVolumes;
        }
    }

    _cout << tr("  PAR2 volumes: %1 intact, %2 damaged (of %3 total)").arg(
                 intactVolumes).arg(damagedVolumes).arg(intactVolumes + damagedVolumes) << "\n";
    _cout << tr("  PAR2 blocks: %1 total, %2 effective (from intact volumes only)").arg(
                 totalBlocks).arg(intactBlocks) << "\n";

    // --- Early return: all PAR2 files damaged (metadata unavailable) ---
    if (!hasIntactPar2Metadata())
    {
        _cout << tr("  WARNING: No intact PAR2 file found - metadata lost!") << "\n";
        int health = computeHealthScore();
        if (_nbMissingDataArticles > 0)
        {
            _cout << tr("  Status: UNRECOVERABLE - PAR2 metadata unavailable") << "\n";
            _cout << "\n" << tr("  Health: %1/100").arg(health) << "\n";
            _cout << tr("  >> Dead - data damaged and PAR2 metadata lost, repair impossible without re-post") << "\n" << MB_FLUSH;
        }
        else
        {
            _cout << tr("  Status: COMPLETE but UNPROTECTED - PAR2 metadata unavailable") << "\n";
            _cout << "\n" << tr("  Health: %1/100").arg(health) << "\n";
            _cout << tr("  >> Critical - data intact but all PAR2 damaged, no recovery or verification possible") << "\n" << MB_FLUSH;
        }
        return;
    }

    // Metadata is available: par2verify can validate data integrity (checksums)
    _cout << tr("  PAR2 metadata: available (integrity verification possible)") << "\n";

    // --- Debug: per-volume damage detail ---
    if (damagedVolumes > 0 && debugMode())
    {
        _cout << tr("  Damaged volumes detail:") << "\n";
        for (const Par2Volume &vol : _par2Volumes)
        {
            if (vol.isVolume && !vol.isIntact())
                _cout << tr("    - %1 blocks LOST (missing %2 article(s)): %3").arg(
                             vol.blocks).arg(vol.nbMissingArticles).arg(vol.subject) << "\n";
        }
    }

    // --- Status line: COMPLETE / RECOVERABLE / UNRECOVERABLE ---
    if (_nbMissingDataArticles == 0)
    {
        _cout << tr("  Status: COMPLETE - No missing data articles") << "\n";
    }
    else if (isRecoverable())
    {
        int damagedBlocks = estimateDamagedBlocks();
        _cout << tr("  Estimated damaged blocks: %1 (block size: %2, article size: %3)").arg(
                     damagedBlocks).arg(_par2BlockSize).arg(_articleSize) << "\n";
        _cout << tr("  Status: RECOVERABLE - %1 damaged block(s), effective recovery blocks: %2").arg(
                     damagedBlocks).arg(intactBlocks) << "\n";
    }
    else
    {
        int damagedBlocks = estimateDamagedBlocks();
        _cout << tr("  Estimated damaged blocks: %1 (block size: %2, article size: %3)").arg(
                     damagedBlocks).arg(_par2BlockSize).arg(_articleSize) << "\n";
        _cout << tr("  Status: UNRECOVERABLE - %1 damaged block(s), effective recovery blocks: %2").arg(
                     damagedBlocks).arg(intactBlocks) << "\n";
    }

    // --- Health score and recommendation ---
    int health = computeHealthScore();
    _cout << "\n" << tr("  Health: %1/100").arg(health);

    // Warning for PAR2 degradation even when data is complete
    // Reports block loss percentage (not article loss) for accuracy
    if (_nbMissingDataArticles == 0 && _nbMissingPar2Articles > 0)
    {
        int lostBlocks = totalBlocks - intactBlocks;
        double blockLoss = (totalBlocks > 0)
            ? (static_cast<double>(lostBlocks) / totalBlocks) * 100.0
            : 0.0;
        _cout << tr(" [WARNING: %1% of PAR2 recovery blocks lost (%2/%3) - recovery capacity degraded]").arg(
                     blockLoss, 0, 'f', 1).arg(lostBlocks).arg(totalBlocks);
    }
    _cout << "\n";

    // Recommendation thresholds aligned with computeHealthScore() caps:
    //   >= 90: healthy (all good)
    //   >= 70: degraded (PAR2 loss but still viable)
    //   >= 50: at risk (severe PAR2 loss)
    //   >= 30: critical (barely viable)
    //   <  30: dead (unrecoverable cap = 25 max)
    if (health >= 90)
        _cout << tr("  >> Healthy - data intact, PAR2 redundancy sufficient") << "\n";
    else if (health >= 70)
        _cout << tr("  >> Degraded - PAR2 redundancy reduced, re-post PAR2 volumes to restore protection") << "\n";
    else if (health >= 50)
        _cout << tr("  >> At risk - significant PAR2 loss, repair possible but redundancy critically low") << "\n";
    else if (health >= 30)
        _cout << tr("  >> Critical - repair barely viable, consider re-posting from source") << "\n";
    else
        _cout << tr("  >> Dead - unrecoverable, must be re-posted from source") << "\n";

    _cout << MB_FLUSH;
}
