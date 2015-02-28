#include "packet_data_save.h"
#include "mainwindow.h"

#include <QVector>
#include <QMap>

PacketDataSaveThread::PacketDataSaveThread(QObject *pObj)
{
	m_pWindow = (MainWindow*)pObj;

	connect(this,SIGNAL(update_progressbar(int)), m_pWindow, SLOT(slot_update_progressbar(int)));
	connect(this,SIGNAL(update_status(UnpackStatus)), m_pWindow, SLOT(slot_update_status(UnpackStatus)));
}

PacketDataSaveThread::~PacketDataSaveThread()
{

}

int64_t PacketDataSaveThread::getFileSize(QString filename)
{
	int64_t filesize = 0;
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly)) return 0;
	filesize = file.size();
	file.close();

	return filesize;
}

void PacketDataSaveThread::run()
{
	AVFormatContext *ic = NULL;
	AVPacket pkt;
	int wanted_stream[AVMEDIA_TYPE_NB];
	int err, ret = 0;
	int64_t filesize = 0, pos_max = 0;

	av_init_packet(&pkt);
	memset(wanted_stream, -1, sizeof(wanted_stream));
	filesize = getFileSize(m_pWindow->m_fileName);

	ic = avformat_alloc_context();
	err = avformat_open_input(&ic, m_pWindow->m_fileName.toStdString().data(), NULL, NULL);
	if(err < 0){
		m_pWindow->setUnpackStatusWithLock(UNPACK_ERROR);
		emit update_status(UNPACK_ERROR);
		goto ERR;
	}
	err = avformat_find_stream_info(ic, NULL);
	if(err < 0){
		m_pWindow->setUnpackStatusWithLock(UNPACK_ERROR);
		emit update_status(UNPACK_ERROR);
		goto ERR;
	}

	int video_index = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, wanted_stream[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
	int audio_index = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, wanted_stream[AVMEDIA_TYPE_AUDIO], video_index, NULL, 0);

	while(1){
		m_pWindow->lock();
		if(m_pWindow->getUnpackStatus() == UNPACK_PAUSE){
			emit update_status(m_pWindow->getUnpackStatus());
			m_pWindow->m_waitCond.wait(&m_pWindow->m_mutex);
			emit update_status(m_pWindow->getUnpackStatus());
		}
		if(m_pWindow->getUnpackStatus() &  (UNPACK_FINISH | UNPACK_STOP | UNPACK_ERROR)){
			emit update_status(m_pWindow->getUnpackStatus());
			m_pWindow->unlock();
			break;
		}
		m_pWindow->unlock();

		ret = av_read_frame(ic, &pkt);
		if(ret < 0 || pkt.size <= 0 || pkt.data == NULL){
			if (ret == AVERROR_EOF || url_feof(ic->pb)){
				m_pWindow->setUnpackStatusWithLock(UNPACK_FINISH);
			}else{
				m_pWindow->setUnpackStatusWithLock(UNPACK_ERROR);
				emit update_status(UNPACK_ERROR);
			}
			break;
		}else{
			if(pkt.dts == AV_NOPTS_VALUE)
				pkt.dts = -1;
			if(pkt.pts == AV_NOPTS_VALUE)
				pkt.pts = -1;
			int64_t pts_tmp = pkt.pts == -1 ? pkt.dts : pkt.pts;

			AVRational time_base = ic->streams[pkt.stream_index]->time_base;
			double pts_sec, dur_sec;
			pts_sec = pts_tmp != -1 ? ((double)pts_tmp * 1.0 * time_base.num / time_base.den) : -1;
			dur_sec = pkt.duration != -1 ? ((double)pkt.duration * 1.0 * time_base.num / time_base.den) : -1;

			if(0 == strcmp(ic->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2")){
				if(pts_sec > 0 && ic->duration > 0){
					long long value = PROGRESS_RANGE * pts_sec * 1000 * 1000 / ic->duration;
					emit update_progressbar((int)value);
				}
			}else{
				if(filesize > 0 && pkt.pos > 0){
					pos_max = qMax(pkt.pos, pos_max);
					if(pos_max < filesize){
						long long value = (PROGRESS_RANGE - 1) * pos_max / filesize;
						emit update_progressbar((int)value);
					}else{
						qDebug() << "pos_max = " << pos_max << ",filesize = " << filesize <<endl;
					}
				}
			}

			av_free_packet(&pkt);
		}
	}

	if(m_pWindow->m_unpackStatus == UNPACK_FINISH){
		emit update_progressbar(PROGRESS_RANGE-1);
		emit update_status(UNPACK_FINISH);
	}

	av_free_packet(&pkt);
	if(ic)
		avformat_close_input(&ic);
	return;
ERR:
	av_free_packet(&pkt);
	if(ic)
		avformat_close_input(&ic);
}
