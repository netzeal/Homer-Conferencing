/*****************************************************************************
 *
 * Copyright (C) 2011 Thomas Volkert <thomas@homer-conferencing.com>
 *
 * This software is free software.
 * Your are allowed to redistribute it and/or modify it under the terms of
 * the GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This source is published in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program. Otherwise, you can write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 * Alternatively, you find an online version of the license text under
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 *****************************************************************************/

/*
 * Purpose: Implementation of OverviewPlaylistWidget
 * Author:  Thomas Volkert
 * Since:   2011-03-22
 */

#include <Widgets/OverviewPlaylistWidget.h>
#include <Configuration.h>
#include <Logger.h>
#include <Snippets.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QTimerEvent>
#include <QHeaderView>
#include <QFileDialog>
#include <QSizePolicy>
#include <QMenu>
#include <QUrl>
#include <QContextMenuEvent>

namespace Homer { namespace Gui {

#define PLAYLIST_UPDATE_DELAY         250 //ms
#define ALLOWED_AV_TIME_DIFF			2 //s

///////////////////////////////////////////////////////////////////////////////

OverviewPlaylistWidget::OverviewPlaylistWidget(QAction *pAssignedAction, QMainWindow* pMainWindow, VideoWorkerThread *pVideoWorker, AudioWorkerThread *pAudioWorker):
    QDockWidget(pMainWindow)
{
    mAssignedAction = pAssignedAction;
    mVideoWorker = pVideoWorker;
    mAudioWorker = pAudioWorker;
    mCurrentFileId = -1;
    mTimerId = -1;
    mIsPlayed = false;
    mEndlessLoop = false;

    initializeGUI();

    setAllowedAreas(Qt::AllDockWidgetAreas);
    pMainWindow->addDockWidget(Qt::LeftDockWidgetArea, this);

    if (mAssignedAction != NULL)
    {
        connect(mAssignedAction, SIGNAL(triggered(bool)), this, SLOT(SetVisible(bool)));
        mAssignedAction->setChecked(false);
    }
    connect(toggleViewAction(), SIGNAL(toggled(bool)), mAssignedAction, SLOT(setChecked(bool)));
    connect(mTbAdd, SIGNAL(clicked()), this, SLOT(AddEntryDialog()));
    connect(mTbDel, SIGNAL(clicked()), this, SLOT(DelEntryDialog()));
    connect(mTbSaveList, SIGNAL(clicked()), this, SLOT(SaveListDialog()));
    connect(mLwFiles, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(Play()));
    mShortcutDel = new QShortcut(Qt::Key_Delete, mLwFiles);
    mShortcutIns = new QShortcut(Qt::Key_Insert, mLwFiles);
    mShortcutDel->setEnabled(true);
    mShortcutIns->setEnabled(true);
    connect(mShortcutDel, SIGNAL(activated()), this, SLOT(DelEntryDialogSc()));
    connect(mShortcutDel, SIGNAL(activatedAmbiguously()), this, SLOT(DelEntryDialogSc()));
    connect(mShortcutIns, SIGNAL(activated()), this, SLOT(AddEntryDialogSc()));
    connect(mShortcutIns, SIGNAL(activatedAmbiguously()), this, SLOT(AddEntryDialogSc()));
    connect(mTbPlay, SIGNAL(clicked()), this, SLOT(ActionPlay()));
    connect(mTbPause, SIGNAL(clicked()), this, SLOT(ActionPause()));
    connect(mTbStop, SIGNAL(clicked()), this, SLOT(ActionStop()));
    connect(mTbNext, SIGNAL(clicked()), this, SLOT(ActionNext()));
    connect(mTbLast, SIGNAL(clicked()), this, SLOT(ActionLast()));
    mTimerId = startTimer(PLAYLIST_UPDATE_DELAY);
    SetVisible(CONF.GetVisibilityPlaylistWidgetMovie());
    mAssignedAction->setChecked(CONF.GetVisibilityPlaylistWidgetMovie());
}

OverviewPlaylistWidget::~OverviewPlaylistWidget()
{
	if (mTimerId != -1)
		killTimer(mTimerId);

    CONF.SetVisibilityPlaylistWidgetMovie(isVisible());
}

///////////////////////////////////////////////////////////////////////////////

void OverviewPlaylistWidget::initializeGUI()
{
    setupUi(this);

    // hide id column
//    mTwFiles->setColumnHidden(5, true);
//    mTwFiles->sortItems(5);
//    mTwFiles->horizontalHeader()->setResizeMode(QHeaderView::Interactive);
//    for (int i = 0; i < 2; i++)
//        mTwFiles->horizontalHeader()->resizeSection(i, mTwFiles->horizontalHeader()->sectionSize(i) * 2);
}

void OverviewPlaylistWidget::closeEvent(QCloseEvent* pEvent)
{
    SetVisible(false);
}

void OverviewPlaylistWidget::SetVisible(bool pVisible)
{
    if (pVisible)
    {
        move(mWinPos);
        show();
    }else
    {
        mWinPos = pos();
        hide();
    }
}

void OverviewPlaylistWidget::StartPlaylist()
{
    if (GetListSize() == 0)
    {
        LOG(LOG_VERBOSE, "Playlist start triggered but we don't have entries in the list, asking user..");
        AddEntryDialog();
    }else
        LOG(LOG_VERBOSE, "Playlist start triggered and we already have entries in the list");

    Play(mCurrentFileId);
}

void OverviewPlaylistWidget::StopPlaylist()
{
	mIsPlayed = false;
}

void OverviewPlaylistWidget::contextMenuEvent(QContextMenuEvent *pContextMenuEvent)
{
    QAction *tAction;

    QMenu tMenu(this);

    tAction = tMenu.addAction("Add an entry");
    QIcon tIcon1;
    tIcon1.addPixmap(QPixmap(":/images/22_22/Plus.png"), QIcon::Normal, QIcon::Off);
    tAction->setIcon(tIcon1);

    if (!mLwFiles->selectedItems().isEmpty())
    {
        tAction = tMenu.addAction("Delete selected");
        QIcon tIcon2;
        tIcon2.addPixmap(QPixmap(":/images/22_22/Minus.png"), QIcon::Normal, QIcon::Off);
        tAction->setIcon(tIcon2);
    }

    tMenu.addSeparator();

    tAction = tMenu.addAction("Endless loop");
    tAction->setCheckable(true);
    tAction->setChecked(mEndlessLoop);

    QAction* tPopupRes = tMenu.exec(pContextMenuEvent->globalPos());
    if (tPopupRes != NULL)
    {
        if (tPopupRes->text().compare("Add an entry") == 0)
        {
            AddEntryDialog();
            return;
        }
        if (tPopupRes->text().compare("Delete selected") == 0)
        {
            DelEntryDialog();
            return;
        }
        if (tPopupRes->text().compare("Endless loop") == 0)
        {
            mEndlessLoop = !mEndlessLoop;
            LOG(LOG_VERBOSE, "Playlist has now endless loop activation %d", mEndlessLoop);
            return;
        }
    }
}

void OverviewPlaylistWidget::DelEntryDialog()
{
    int tSelectectRow = -1;

    if (mLwFiles->selectionModel()->currentIndex().isValid())
    {
        int tSelectedRow = mLwFiles->selectionModel()->currentIndex().row();
        DeleteListEntry(tSelectedRow);
        UpdateView();
    }
}

static QString sAllLoadVideoFilter = "All supported formats (*.asf *.avi *.dv *.m4v *.mkv *.mov *.mpg *.mpeg *.mp4 *.mp4a *.m3u *.swf *.vob *.wmv *.3gp)";
static QString sLoadVideoFilters = sAllLoadVideoFilter + ";;"\
                    "Advanced Systems Format (*.asf);;"\
                    "Audio Video Interleave Format (*.avi);;"\
                    "Digital Video Format (*.dv);;"\
                    "Matroska Format (*.mkv);;"\
                    "MPEG-Program Stream Format (*.mpg *.mpeg);;"\
                    "Playlist file (*.m3u);;"\
                    "Quicktime/MPEG4 Format (*.m4v *.mov *.mp4 *.mp4a *.3gp);;"\
                    "Small Web Format (*.swf);;"\
                    "Video Object Format (*.vob);;" \
                    "Windows Media Video Format (*.wmv)";

QStringList OverviewPlaylistWidget::LetUserSelectVideoFile(QWidget *pParent, QString pDescription, bool pMultipleFiles)
{
    QStringList tResult;

    if (pMultipleFiles)
        tResult = QFileDialog::getOpenFileNames(pParent, pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Video.avi",
                                                                sLoadVideoFilters,
                                                                &sAllLoadVideoFilter,
                                                                CONF_NATIVE_DIALOGS);
    else
        tResult = QStringList(QFileDialog::getOpenFileName(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Video.avi",
                                                                sLoadVideoFilters,
                                                                &sAllLoadVideoFilter,
                                                                CONF_NATIVE_DIALOGS));

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.first().left(tResult.first().lastIndexOf('/')));

    return tResult;
}

static QString sAllSaveVideoFilter = "All supported formats (*.avi *.m4v *.mov *.mp4 *.mp4a *.3gp)";
static QString sSaveVideoFilters = sAllSaveVideoFilter + ";;"\
                    "Audio Video Interleave Format (*.avi);;"\
                    "Quicktime/MPEG4 Format (*.m4v *.mov *.mp4 *.mp4a *.3gp)";

QString OverviewPlaylistWidget::LetUserSelectVideoSaveFile(QWidget *pParent, QString pDescription)
{
    QString tResult = QFileDialog::getSaveFileName(pParent,  pDescription,
                                                            CONF.GetDataDirectory() + "/Homer-Video.avi",
                                                            sSaveVideoFilters,
                                                            &sAllSaveVideoFilter,
                                                            CONF_NATIVE_DIALOGS);

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.left(tResult.lastIndexOf('/')));

    return tResult;
}

bool OverviewPlaylistWidget::IsVideoFile(QString pFileName)
{
    pFileName = QString(pFileName.toLocal8Bit());

    int tPos = pFileName.lastIndexOf('.', -1);
    if (tPos == -1)
    {
        LOGEX(OverviewPlaylistWidget, LOG_ERROR, "Video file name lacks a correct format selecting end");
        return false;
    }

    QString tExt = pFileName.right(pFileName.size() - tPos).toLower();
    LOGEX(OverviewPlaylistWidget, LOG_VERBOSE, "Checking for video content in file %s of type %s", pFileName.toStdString().c_str(), tExt.toStdString().c_str());

    if (sLoadVideoFilters.indexOf(tExt, 0) != -1)
        return true;
    else
        return false;
}

static QString sAllLoadAudioFilter =  "All supported formats (*.mp3 *.avi *.mka *.mkv *.m3u *.wav)";
static QString sLoadAudioFilters =  sAllLoadAudioFilter + ";;"\
                    "Audio Video Interleave Format (*.avi);;"\
                    "Matroska Format (*.mka);;"\
                    "MPEG Audio Layer 2/3 Format (*.mp3);;"\
                    "Playlist file (*.m3u);;"\
                    "Waveform Audio File Format (*.wav)";

QStringList OverviewPlaylistWidget::LetUserSelectAudioFile(QWidget *pParent, QString pDescription, bool pMultipleFiles)
{
    QStringList tResult;

    if (pMultipleFiles)
        tResult = QFileDialog::getOpenFileNames(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Audio.mp3",
                                                                sLoadAudioFilters,
                                                                &sAllLoadAudioFilter,
                                                                CONF_NATIVE_DIALOGS);
    else
        tResult = QStringList(QFileDialog::getOpenFileName(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Audio.mp3",
                                                                sLoadAudioFilters,
                                                                &sAllLoadAudioFilter,
                                                                CONF_NATIVE_DIALOGS));

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.first().left(tResult.first().lastIndexOf('/')));

    return tResult;
}

static QString sAllSaveAudioFilter =  "All supported formats (*.mp3 *.wav)";
static QString sSaveAudioFilters =  sAllSaveAudioFilter + ";;"\
                    "MPEG Audio Layer 2/3 Format (*.mp3);;"\
                    "Waveform Audio File Format (*.wav)";

QString OverviewPlaylistWidget::LetUserSelectAudioSaveFile(QWidget *pParent, QString pDescription)
{
    QString tResult = QFileDialog::getSaveFileName(pParent,  pDescription,
                                                            CONF.GetDataDirectory() + "/Homer-Audio.mp3",
                                                            sSaveAudioFilters,
                                                            &sAllSaveAudioFilter,
                                                            CONF_NATIVE_DIALOGS);

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.left(tResult.lastIndexOf('/')));

    return tResult;
}

bool OverviewPlaylistWidget::IsAudioFile(QString pFileName)
{
    pFileName = QString(pFileName.toLocal8Bit());

    int tPos = pFileName.lastIndexOf('.', -1);
    if (tPos == -1)
    {
        LOGEX(OverviewPlaylistWidget, LOG_ERROR, "Audio file name lacks a correct format selecting end");
        return false;
    }

    QString tExt = pFileName.right(pFileName.size() - tPos).toLower();
    LOGEX(OverviewPlaylistWidget, LOG_VERBOSE, "Checking for audio content in file %s of type %s", pFileName.toStdString().c_str(), tExt.toStdString().c_str());

    if (sLoadAudioFilters.indexOf(tExt, 0) != -1)
        return true;
    else
        return false;
}

QString sAllLoadMovieFilter = "All supported formats (*.avi *.m4v *.mkv *.mov *.mpeg *.mp4 *.mp4a *.m3u *.swf *.vob *.wmv *.3gp)";
QString sLoadMovieFilters =  sAllLoadMovieFilter + ";;"\
                    "Audio Video Interleave Format (*.avi);;"\
                    "Matroska Format (*.mkv);;"\
                    "MPEG-Program Stream Format (*.mpeg);;"\
                    "Playlist file (*.m3u);;"\
                    "Quicktime/MPEG4 Format (*.m4v *.mov *.mp4 *.mp4a *.3gp);;"\
                    "Small Web Format (*.swf);;"\
                    "Video Object Format (*.vob);;" \
                    "Windows Media Video Format (*.wmv)";

QStringList OverviewPlaylistWidget::LetUserSelectMovieFile(QWidget *pParent, QString pDescription, bool pMultipleFiles)
{
    QStringList tResult;

    if (pMultipleFiles)
        tResult = QFileDialog::getOpenFileNames(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Movie.avi",
                                                                sLoadMovieFilters,
                                                                &sAllLoadMovieFilter,
                                                                CONF_NATIVE_DIALOGS);
    else
        tResult = QStringList(QFileDialog::getOpenFileName(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Movie.avi",
                                                                sLoadMovieFilters,
                                                                &sAllLoadMovieFilter,
                                                                CONF_NATIVE_DIALOGS));

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.first().left(tResult.first().lastIndexOf('/')));

    return tResult;
}

static QString sAllLoadMediaFilter = "All supported formats (*.asf *.avi *.dv *.m4v *.mka *.mkv *.mov *.mpg *.mpeg *.mp3 *.mp4 *.mp4a *.m3u *.pls *.swf *.vob *.wav *.wmv *.3gp)";
static QString sLoadMediaFilters = sAllLoadMediaFilter + ";;"\
                    "Advanced Systems Format (*.asf);;"\
                    "Audio Video Interleave Format (*.avi);;"\
                    "Digital Video Format (*.dv);;"\
                    "Matroska Format (*.mka *.mkv);;"\
                    "MPEG Audio Layer 2/3 Format (*.mp3);;"\
                    "MPEG-Program Stream Format (*.mpg *.mpeg);;"\
                    "M3U Playlist file (*.m3u);;"\
                    "PLS Playlist file (*.pls);;"\
                    "Quicktime/MPEG4 Format (*.m4v *.mov *.mp4 *.mp4a *.3gp);;"\
                    "Small Web Format (*.swf);;"\
                    "Video Object Format (*.vob);;" \
                    "Waveform Audio File Format (*.wav)" \
                    "Windows Media Video Format (*.wmv)";
QStringList OverviewPlaylistWidget::LetUserSelectMediaFile(QWidget *pParent, QString pDescription, bool pMultipleFiles)
{
    QStringList tResult;

    if (pMultipleFiles)
        tResult = QFileDialog::getOpenFileNames(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Movie.avi",
                                                                sLoadMediaFilters,
                                                                &sAllLoadMediaFilter,
                                                                CONF_NATIVE_DIALOGS);
    else
        tResult = QStringList(QFileDialog::getOpenFileName(pParent,  pDescription,
                                                                CONF.GetDataDirectory() + "/Homer-Movie.avi",
                                                                sLoadMediaFilters,
                                                                &sAllLoadMediaFilter,
                                                                CONF_NATIVE_DIALOGS));

    if (!tResult.isEmpty())
        CONF.SetDataDirectory(tResult.first().left(tResult.first().lastIndexOf('/')));

    return tResult;
}

void OverviewPlaylistWidget::AddEntryDialog()
{
    LOG(LOG_VERBOSE, "User wants to add a new entry to playlist");

    bool tListWasEmpty = (mLwFiles->count() == 0);

	QStringList tFileNames;

    tFileNames = LetUserSelectMediaFile(this,  "Add media files to playlist");

    if (tFileNames.isEmpty())
        return;

    QString tFile;
    foreach(tFile, tFileNames)
    {
        AddEntry(tFile);
    }

    if((tListWasEmpty) && (mLwFiles->count() > 0))
    {
        mCurrentFileId = 0;
        LOG(LOG_VERBOSE, "Setting to file %d in playlist", mCurrentFileId);
        mLwFiles->setCurrentRow(mCurrentFileId, QItemSelectionModel::Clear | QItemSelectionModel::Select);
        if (!isVisible())
        	SetVisible(true);
    }
}

void OverviewPlaylistWidget::AddEntryDialogSc()
{
    if (mLwFiles->hasFocus())
        AddEntryDialog();
}

void OverviewPlaylistWidget::DelEntryDialogSc()
{
    if (mLwFiles->hasFocus())
        DelEntryDialog();
}

void OverviewPlaylistWidget::SaveListDialog()
{
    if (GetListSize() < 1)
        return;

    QString tFileName;
    tFileName = QFileDialog::getSaveFileName(this,  "Save playlist to..",
                                                                CONF.GetDataDirectory() + "/Homer.m3u",
                                                                "Playlist file (*.m3u)",
                                                                &*(new QString("Playlist file (*.m3u)")),
                                                                CONF_NATIVE_DIALOGS);

    if (tFileName.isEmpty())
        return;

    QString tPlaylistData;
    PlaylistEntry tEntry;
    mPlaylistMutex.lock();
    foreach(tEntry, mPlaylist)
    {
        QString tPlaylistEntry = tEntry.Location;
        LOG(LOG_VERBOSE, "Writing to m3u %s the entry %s", tFileName.toStdString().c_str(), tPlaylistEntry.toStdString().c_str());
        tPlaylistData += tPlaylistEntry + '\n';

    }
    mPlaylistMutex.unlock();

    QFile tPlaylistFile(tFileName);
    if (!tPlaylistFile.open(QIODevice::WriteOnly))
    {
    	ShowError("Could not store playlist file", "Couldn't write playlist in " + tFileName);
        return;
    }

    tPlaylistFile.write(tPlaylistData.toUtf8());
    tPlaylistFile.close();
}

void OverviewPlaylistWidget::Play(int pIndex)
{
    LOG(LOG_VERBOSE, "Got trigger to play entry %d", pIndex);

    if (pIndex == -1)
	{
	    if (mLwFiles->selectionModel()->currentIndex().isValid())
	        pIndex = mLwFiles->selectionModel()->currentIndex().row();
	}

    if ((pIndex == -1) && (GetListSize() > 0))
    {
        pIndex = 0;
    }

    if (pIndex == -1)
	{
	    LOG(LOG_VERBOSE, "Index is invalid, playback start skipped");
	    return;
	}

	mIsPlayed = true;
	mCurrentFile = GetListEntry(pIndex);

	// VIDEO: we don't support video streaming yet, otherwise we play the file
	if (!mCurrentFile.startsWith("http://"))
        mVideoWorker->PlayFile(mCurrentFile);
	// AUDIO: play the file
	mAudioWorker->PlayFile(mCurrentFile);

    mCurrentFileId = pIndex;
    LOG(LOG_VERBOSE, "Setting current row to %d in playlist", mCurrentFileId);
    mLwFiles->setCurrentRow(mCurrentFileId, QItemSelectionModel::Clear | QItemSelectionModel::Select);
}

void OverviewPlaylistWidget::PlayNext()
{
    int tNewFileId = -1;

    // derive file id of next file which should be played
	if (mCurrentFileId < GetListSize() -1)
    {
		tNewFileId = mCurrentFileId + 1;
    }else
    {
    	if (mEndlessLoop)
    	{
    		tNewFileId = 0;
    	}else
    	{
    		//LOG(LOG_VERBOSE, "End of playlist reached");
    		return;
    	}
    }

	LOG(LOG_VERBOSE, "Playing file entry %d", tNewFileId);

	// finally play the next file
    Play(tNewFileId);
}

void OverviewPlaylistWidget::PlayLast()
{
    if (mCurrentFileId > 0)
        Play(mCurrentFileId - 1);
}

void OverviewPlaylistWidget::timerEvent(QTimerEvent *pEvent)
{
    #ifdef DEBUG_TIMING
        LOG(LOG_VERBOSE, "New timer event");
    #endif
    if (pEvent->timerId() == mTimerId)
    {

		#ifdef SYNCHRONIZE_AUDIO_VIDEO
			// synch. video and audio by seeking in audio stream based on the position of the video stream, the other way round it would be more time consuming!
			int64_t tTimeDiff = mVideoWorker->GetSeekPos() - mAudioWorker->GetSeekPos();
			if (((tTimeDiff < -ALLOWED_AV_TIME_DIFF) || (tTimeDiff > ALLOWED_AV_TIME_DIFF)))
			{
				if ((!mVideoWorker->EofReached()) && (!mAudioWorker->EofReached()) && (mVideoWorker->GetCurrentDevice() == mAudioWorker->GetCurrentDevice()))
				{
					LOG(LOG_ERROR, "AV stream asynchronous (difference: %lld seconds, max is %d seconds), synchronizing now..", tTimeDiff, ALLOWED_AV_TIME_DIFF);
					mAudioWorker->Seek(mVideoWorker->GetSeekPos());
				}
			}
		#endif

    	// play next if EOF is reached
        // stop if current file wasn't yet switched to the desired one;
        if ((mVideoWorker->CurrentFile() != mCurrentFile) || (mAudioWorker->CurrentFile() != mCurrentFile))
            return;

        // do we already play the desired file and are we at EOF?
        if ((mCurrentFileId != -1) &&
            (mVideoWorker->GetCurrentDevice().contains(GetListEntry(mCurrentFileId))) && (mVideoWorker->EofReached()) &&
            (mAudioWorker->GetCurrentDevice().contains(GetListEntry(mCurrentFileId))) && (mAudioWorker->EofReached()))
        {
            PlayNext();
        }
    }
}

void OverviewPlaylistWidget::dragEnterEvent(QDragEnterEvent *pEvent)
{
    if (pEvent->mimeData()->hasUrls())
    {
        pEvent->acceptProposedAction();
        QList<QUrl> tList = pEvent->mimeData()->urls();
        QUrl tUrl;
        int i = 0;

        foreach(tUrl, tList)
            LOG(LOG_VERBOSE, "New drag+drop url (%d) \"%s\"", ++i, tUrl.toString().toStdString().c_str());
        return;
    }
}

void OverviewPlaylistWidget::dropEvent(QDropEvent *pEvent)
{
    bool tListWasEmpty = (GetListSize() == 0);

    if (pEvent->mimeData()->hasUrls())
    {
        LOG(LOG_VERBOSE, "Got some dropped urls");
        QList<QUrl> tUrlList = pEvent->mimeData()->urls();
        QUrl tUrl;
        foreach(tUrl, tUrlList)
        {
            AddEntry(tUrl.toLocalFile());
        }
        pEvent->acceptProposedAction();
        return;
    }

    if ((tListWasEmpty) && (GetListSize() > 0) && (mIsPlayed))
        Play(mCurrentFileId);
}

int OverviewPlaylistWidget::GetListSize()
{
    int tResult = 0;

    mPlaylistMutex.lock();
    tResult = mPlaylist.size();
    mPlaylistMutex.unlock();

    return tResult;
}

void OverviewPlaylistWidget::AddEntry(QString pLocation, QString pName)
{
    if (pLocation.endsWith(".m3u"))
    {
        AddM3UToList(pLocation);
    }else if (pLocation.endsWith(".pls"))
    {
        AddPLSToList(pLocation);
    }else
    {
        if (pName == "")
        {
            if (!pLocation.startsWith("http://"))
            {
                int tPos = pLocation.lastIndexOf('\\');
                if (tPos == -1)
                    tPos = pLocation.lastIndexOf('/');
                if (tPos != -1)
                {
                    tPos += 1;
                    pName = pLocation.mid(tPos, pLocation.size() - tPos);
                }else
                    pName = pLocation;
            }else
                pName = pLocation;
        }

        LOG(LOG_VERBOSE, "Adding to playlist: %s at location %s", pName.toStdString().c_str(), pLocation.toStdString().c_str());

        // create playlist entry
        PlaylistEntry tEntry;
        tEntry.Name = pName;
        tEntry.Location = pLocation;
        if (pLocation.startsWith("http://"))
            tEntry.Icon = QIcon(":/images/22_22/NetworkConnection.png");
        else
            tEntry.Icon = QIcon(":/images/22_22/ArrowRight.png");

        // save playlist entry
        mPlaylistMutex.lock();
        mPlaylist.push_back(tEntry);
        mPlaylistMutex.unlock();

        // trigger GUI update
        QApplication::postEvent(this, new QEvent(QEvent::User));
    }
}

void OverviewPlaylistWidget::AddM3UToList(QString pFilePlaylist)
{
    QString tDir = pFilePlaylist.left(pFilePlaylist.lastIndexOf('/'));
    LOG(LOG_VERBOSE, "Opening M3U playlist file %s", pFilePlaylist.toStdString().c_str());
    LOG(LOG_VERBOSE, "..in directory: %s", tDir.toStdString().c_str());

    QFile tPlaylistFile(pFilePlaylist);
    if (!tPlaylistFile.open(QIODevice::ReadOnly))
    {
        LOG(LOG_ERROR, "Couldn't read M3U playlist from %s", pFilePlaylist.toStdString().c_str());
    }else
    {
        QByteArray tLine;
        tLine = tPlaylistFile.readLine();
        while (!tLine.isEmpty())
        {
            QString tLineString = QString(tLine);

            // remove line delimiters
            while((tLineString.endsWith(QChar(0x0A))) || (tLineString.endsWith(QChar(0x0D))))
                tLineString = tLineString.left(tLineString.length() - 1); //remove any "new line" char from the end

            // parse the playlist line
            if (!tLineString.startsWith("#EXT"))
            {
                LOG(LOG_VERBOSE, "Found playlist entry: %s", tLineString.toStdString().c_str());
                if (tLineString.startsWith("http://"))
                    AddEntry(tLineString);
                else
                    AddEntry(tDir + "/" + tLineString);
            }else
                LOG(LOG_VERBOSE, "Found playlist extended entry: %s", tLineString.toStdString().c_str());

            tLine = tPlaylistFile.readLine();
        }
    }
}

void OverviewPlaylistWidget::AddPLSToList(QString pFilePlaylist)
{
    QString tDir = pFilePlaylist.left(pFilePlaylist.lastIndexOf('/'));
    LOG(LOG_VERBOSE, "Opening PLS playlist file %s", pFilePlaylist.toStdString().c_str());
    LOG(LOG_VERBOSE, "..in directory: %s", tDir.toStdString().c_str());

    QFile tPlaylistFile(pFilePlaylist);
    if (!tPlaylistFile.open(QIODevice::ReadOnly))
    {
        LOG(LOG_ERROR, "Couldn't read PLS playlist from %s", pFilePlaylist.toStdString().c_str());
    }else
    {
        QByteArray tLine;
        tLine = tPlaylistFile.readLine();
        int tPlaylistEntries = 0;
        int tFoundPlaylisEntries = -1;
        bool tPlaylistEntryParsed = false;
        PlaylistEntry tPlaylistEntry;
        while ((tFoundPlaylisEntries < tPlaylistEntries) && (!tLine.isEmpty()))
        {
            QString tLineString = QString(tLine);

            // remove line delimiters
            while((tLineString.endsWith(QChar(0x0A))) || (tLineString.endsWith(QChar(0x0D))))
                tLineString = tLineString.left(tLineString.length() - 1); //remove any "new line" char from the end

            QStringList tLineSplit = tLineString.split(("="));
            if (tLineSplit.size() == 2)
            {
                QString tKey = tLineSplit[0];
                QString tValue = tLineSplit[1];
                LOG(LOG_VERBOSE, "Found key \"%s\" with value \"%s\"", tKey.toStdString().c_str(), tValue.toStdString().c_str());
                // parse the playlist line
                if (tKey.startsWith("NumberOfEntries"))
                {// "NumberOfEntries"
                    bool tConversionWasOkay = false;
                    tPlaylistEntries = tValue.toInt(&tConversionWasOkay);
                    if (!tConversionWasOkay)
                    {
                        LOG(LOG_ERROR, "Unable to convert \"%s\" into an integer value", tValue.toStdString().c_str());
                        return;
                    }
                }else if (tKey.startsWith("File"))
                {// "File"
                    tPlaylistEntry.Location = tValue;
                }else if (tKey.startsWith("Title"))
                {// "File"
                    tFoundPlaylisEntries++;
                    tPlaylistEntryParsed = true;
                    tPlaylistEntry.Name = tValue;
                }
                if (tPlaylistEntryParsed)
                {
                    tPlaylistEntryParsed = false;
                    LOG(LOG_VERBOSE, "Found playlist entry: \"%s\" at location \"%s\"", tPlaylistEntry.Name.toStdString().c_str(), tPlaylistEntry.Location.toStdString().c_str());
                    AddEntry(tPlaylistEntry.Location, tPlaylistEntry.Name);
                }
            }else
            {
                if (tLineString.startsWith("["))
                {// "[playlist]"
                    // nothing to do
                }else
                {
                    LOG(LOG_VERBOSE, "Unexpected token in PLS playlist: \"%s\"", tLineString.toStdString().c_str());
                }
            }

            tLine = tPlaylistFile.readLine();
        }
    }
}

QString OverviewPlaylistWidget::GetListEntry(int pIndex)
{
    QString tResult = "";

    mPlaylistMutex.lock();
    PlaylistEntry tEntry;
    int tIndex = 0;
    foreach(tEntry, mPlaylist)
    {
        if (tIndex == pIndex)
        {
            tResult = tEntry.Location;
            break;
        }
        tIndex++;
    }
    mPlaylistMutex.unlock();

    return tResult;
}

void OverviewPlaylistWidget::DeleteListEntry(int pIndex)
{
    int tIndex = 0;
    Playlist::iterator tIt;

    mPlaylistMutex.lock();

    if (mPlaylist.size() > 0)
    {
        for (tIt = mPlaylist.begin(); tIt != mPlaylist.end(); tIt++)
        {
            if (tIndex == pIndex)
            {
                mPlaylist.erase(tIt);
                break;
            }
            tIndex++;
        }
    }

    mPlaylistMutex.unlock();
}

void OverviewPlaylistWidget::ActionPlay()
{
	LOG(LOG_VERBOSE, "Triggered play");
	Play(mCurrentFileId);
}

void OverviewPlaylistWidget::ActionPause()
{
	if (!mIsPlayed)
		return;

	LOG(LOG_VERBOSE, "Triggered pause");
    mVideoWorker->PauseFile();
    mAudioWorker->PauseFile();
}

void OverviewPlaylistWidget::ActionStop()
{
	if (!mIsPlayed)
		return;

	LOG(LOG_VERBOSE, "Triggered stop");
    mVideoWorker->StopFile();
    mAudioWorker->StopFile();
}

void OverviewPlaylistWidget::ActionLast()
{
	if (!mIsPlayed)
		return;

	LOG(LOG_VERBOSE, "Triggered last");
	PlayLast();
}

void OverviewPlaylistWidget::ActionNext()
{
	if (!mIsPlayed)
		return;

	LOG(LOG_VERBOSE, "Triggered next");
	PlayNext();
}

void OverviewPlaylistWidget::FillRow(int pRow, const PlaylistEntry &pEntry)
{
    if (mLwFiles->item(pRow) != NULL)
        mLwFiles->item(pRow)->setText(pEntry.Name);
    else
    {
        QListWidgetItem *tItem = new QListWidgetItem(pEntry.Icon, pEntry.Name);
        tItem->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        mLwFiles->insertItem(pRow, tItem);
    }
}

void OverviewPlaylistWidget::UpdateView()
{
    Playlist::iterator tIt;
    int tRow = 0;
    int tSelectedRow = -1;

    //LOG(LOG_VERBOSE, "Updating view");

    if (mLwFiles->selectionModel()->currentIndex().isValid())
        tSelectedRow = mLwFiles->currentRow();

    if (GetListSize() != mLwFiles->count())
    {
        mLwFiles->clear();
    }

    mPlaylistMutex.lock();

    if (mPlaylist.size() > 0)
    {
        PlaylistEntry tEntry;
        foreach(tEntry, mPlaylist)
        {
            FillRow(tRow++, tEntry);
        }
    }

    mPlaylistMutex.unlock();

    if (tSelectedRow != -1)
        mLwFiles->setCurrentRow (tSelectedRow);
}

void OverviewPlaylistWidget::customEvent(QEvent* pEvent)
{
    if (pEvent->type() != QEvent::User)
    {
        LOG(LOG_ERROR, "Wrong Qt event type detected");
        return;
    }

    UpdateView();
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace
