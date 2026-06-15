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

#ifndef NZBCHECK_H
#define NZBCHECK_H
#include <QObject>
#include <QStack>
#include <QString>
#include <QTextStream>
#include <QSet>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QCommandLineOption>
#include <QElapsedTimer>
struct NntpServerParams;
class NntpCheckCon;

#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    #define MB_FLUSH flush
#else
    #define MB_FLUSH Qt::flush
#endif

/*!
 * \brief Represents a single PAR2 file (base or volume) in the NZB
 */
struct Par2Volume {
    QString subject;             //!< subject line identifying this par2 file
    int     blocks;              //!< number of recovery blocks (0 for .par2 base)
    bool    isVolume;            //!< true if it's a .vol*.par2, false if it's the base .par2
    QSet<QString> articleIds;    //!< all article message-ids belonging to this file
    int     nbExpectedArticles;  //!< total articles expected for this file
    int     nbMissingArticles;   //!< articles missing from server (updated during check)

    Par2Volume() : blocks(0), isVolume(false), nbExpectedArticles(0), nbMissingArticles(0) {}

    //! A volume is intact if none of its articles are missing
    bool isIntact() const { return nbMissingArticles == 0; }
};

class NzbCheck : public QObject
{
    Q_OBJECT

private:
    static constexpr const char *sNntpArticleYencSubjectStrRegExp = "yEnc\\s+\\(\\d+/(\\d+)\\)";

    QString           _nzbPath;
    QStack<QString>   _par2ArticleQueue;   //!< PAR2 articles (checked first)
    QStack<QString>   _dataArticleQueue;   //!< Data articles (checked after PAR2 phase)
    QStack<QString>   _articles;           //!< unified view (deprecated, kept for count)

    QTextStream       _cout; //!< stream for stdout
    QTextStream       _cerr; //!< stream for stderr

    int               _nbTotalArticles;
    int               _nbMissingArticles;
    int               _nbCheckedArticles;


    QList<NntpServerParams*> _nntpServers; //!< the servers parameters

    ushort            _debug;

    QSet<NntpCheckCon*>    _connections;

    bool              _dispProgressBar;
    QTimer            _progressbarTimer;      //!< timer to refresh the upload information (progressbar bar, avg. speed)
    const int         _refreshRate;           //!< refresh rate

    bool              _quietMode;

    QElapsedTimer     _timeStart;
    int               _nbCons;
    ushort            _nbMaxRetry;    //!< max retries per connection (same as retry config)
    int               _socketTimeOut; //!< socket timeout in ms

    // PAR2 recovery analysis (per-volume tracking)
    int               _nbPar2Articles;         //!< total articles belonging to par2 files
    int               _nbDataArticles;         //!< total articles belonging to data files
    int               _nbMissingPar2Articles;  //!< missing articles from par2 files (total)
    int               _nbMissingDataArticles;  //!< missing articles from data files
    QVector<Par2Volume> _par2Volumes;          //!< individual par2 volumes for precise tracking
    QMap<QString, int>  _articleToVolume;      //!< maps article-id to index in _par2Volumes
    qint64            _par2BlockSize;          //!< PAR2 block size in bytes (default: article size ~700KB)
    qint64            _articleSize;            //!< article size in bytes (for block/article ratio calculation)
    bool              _par2PhaseComplete;     //!< true once all PAR2 articles have been checked
    bool              _earlyStopTriggered;   //!< true when irrecoverable state detected → stop checking
    int               _nbPar2Checked;        //!< number of PAR2 articles fully checked so far

    static const int sDefaultRefreshRate  = 200; //!< how often shall we refresh the progressbar bar?
    static const int sprogressbarBarWidth = 50;
    static const QRegularExpression sNntpArticleYencSubjectRegExp;


public slots:
    void onDisconnected(NntpCheckCon *con);
    void onRefreshprogressbarBar();


public:
    NzbCheck();
    ~NzbCheck();

    int parseNzb();
    void checkPost();
    int nbCheckingServers();


    // For ngPost integration
    inline int parseNzb(const QString &nzbPath);
    inline void checkPost(const QList<NntpServerParams*> &nntpServers);
    inline void setDispProgressBar(bool display);
    inline void setQuiet(bool quiet);
    inline void setNbMaxRetry(ushort nbMax);
    inline void setSocketTimeOut(int timeoutMs);

    inline ushort nbMaxRetry() const;
    inline int socketTimeOut() const;


    inline void missingArticle(const QString &article);
    inline QString getNextArticle();
    inline void articleChecked();
    inline bool shouldStopEarly() const;

    inline int nbMissingArticles() const;
    inline bool debugMode() const;
    inline void setDebug(ushort level);

    // PAR2 recovery analysis
    inline bool isPar2Article(const QString &articleId) const;
    inline void setPar2BlockSize(qint64 blockSize);
    inline void setArticleSize(qint64 artSize);
    int estimateDamagedBlocks() const;
    int effectiveRecoveryBlocks() const;
    bool hasIntactPar2Metadata() const;
    bool isRecoverable() const;
    int computeHealthScore() const;
    void printRecoveryAnalysis();


    inline void log(const QString     &aMsg);
    inline void log(const char        *aMsg);
    inline void log(const std::string &aMsg);
    inline void error(const QString     &aMsg);
    inline void error(const char        *aMsg);
    inline void error(const std::string &aMsg);
};

int NzbCheck::parseNzb(const QString &nzbPath)
{
    _nzbPath = nzbPath;
    return parseNzb();
}

void NzbCheck::checkPost(const QList<NntpServerParams *> &nntpServers)
{
    _nntpServers = nntpServers;
    checkPost();
}

void NzbCheck::setDispProgressBar(bool display) { _dispProgressBar = display; }
void NzbCheck::setQuiet(bool quiet) { _quietMode = quiet; }
void NzbCheck::setNbMaxRetry(ushort nbMax) { _nbMaxRetry = nbMax; }
void NzbCheck::setSocketTimeOut(int timeoutMs) { _socketTimeOut = timeoutMs; }

ushort NzbCheck::nbMaxRetry() const { return _nbMaxRetry; }
int NzbCheck::socketTimeOut() const { return _socketTimeOut; }

void NzbCheck::missingArticle(const QString &article)
{
    if (debugMode())
        _cout << (_dispProgressBar ? "\n" : "")
              << tr("+ Missing Article on server: ") << article << "\n" << MB_FLUSH;
    ++_nbMissingArticles;
    if (_articleToVolume.contains(article))
    {
        ++_nbMissingPar2Articles;
        _par2Volumes[_articleToVolume[article]].nbMissingArticles++;
    }
    else
        ++_nbMissingDataArticles;

    // Early-stop evaluation (only after PAR2 phase is complete)
    if (!_earlyStopTriggered && _par2PhaseComplete)
    {
        if (_nbMissingDataArticles > 0 && !isRecoverable())
            _earlyStopTriggered = true;
    }
    // If no PAR2 at all, any missing data article is immediately irrecoverable
    if (!_earlyStopTriggered && _par2Volumes.isEmpty() && _nbMissingDataArticles > 0)
        _earlyStopTriggered = true;
}

QString NzbCheck::getNextArticle()
{
    // Early-stop: signal connections to close
    if (_earlyStopTriggered)
        return QString();

    // Phase 1: PAR2 articles first
    if (!_par2ArticleQueue.isEmpty())
        return _par2ArticleQueue.pop();

    // Transition: PAR2 phase just completed
    if (!_par2PhaseComplete)
    {
        _par2PhaseComplete = true;
        // Immediate check: if no PAR2 exists and data is already missing, stop
        if (_par2Volumes.isEmpty() && _nbMissingDataArticles > 0)
        {
            _earlyStopTriggered = true;
            return QString();
        }
        // Check if already irrecoverable based on PAR2 results
        if (_nbMissingDataArticles > 0 && !isRecoverable())
        {
            _earlyStopTriggered = true;
            return QString();
        }
    }

    // Phase 2: Data articles
    if (!_dataArticleQueue.isEmpty())
        return _dataArticleQueue.pop();

    return QString();
}

void NzbCheck::articleChecked() { ++_nbCheckedArticles; }

bool NzbCheck::shouldStopEarly() const { return _earlyStopTriggered; }

int NzbCheck::nbMissingArticles() const { return _nbMissingArticles; }

bool NzbCheck::debugMode() const { return _debug != 0; }
void NzbCheck::setDebug(ushort level) { _debug = level; }

void NzbCheck::log(const QString     &aMsg) { _cout << aMsg << "\n" << MB_FLUSH; }
void NzbCheck::log(const char        *aMsg) { _cout << aMsg << "\n" << MB_FLUSH; }
void NzbCheck::log(const std::string &aMsg) { _cout << aMsg.c_str() << "\n" << MB_FLUSH; }

void NzbCheck::error(const QString     &aMsg) { _cerr << aMsg << "\n" << MB_FLUSH; }
void NzbCheck::error(const char        *aMsg) { _cerr << aMsg << "\n" << MB_FLUSH; }
void NzbCheck::error(const std::string &aMsg) { _cerr << aMsg.c_str() << "\n" << MB_FLUSH; }

bool NzbCheck::isPar2Article(const QString &articleId) const { return _articleToVolume.contains(articleId); }

void NzbCheck::setPar2BlockSize(qint64 blockSize) { _par2BlockSize = blockSize; }
void NzbCheck::setArticleSize(qint64 artSize) { _articleSize = artSize; }

#endif // NZBCHECK_H
