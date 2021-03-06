/*
  Copyright (c) 2015-2016 University of Helsinki

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <QDebug>
#include <QDateTime>
#include <QTextStream>
#include <QLinkedList>

#include "boost/date_time/posix_time/posix_time.hpp"

#include "camerathread.h"

using namespace boost::posix_time;
using namespace cv;

// ---------------------------------------------------------------------

CameraThread::CameraThread(int i) : idx(i), is_active(false),
				    was_active(false)
{
    setDefaultDesiredInputSize();
    window_size = Size(240,135);
}

// ---------------------------------------------------------------------

CameraThread::CameraThread(int i, QString wxh) : idx(i),
						 is_active(false),
						 was_active(false)
{
  if (wxh.contains('x')) {
    QStringList wh = wxh.split('x');
    bool ok = true;
    desired_input_size.width = wh.at(0).toInt(&ok);
    desired_input_size.height = wh.at(1).toInt(&ok);
    if (!ok)
      setDefaultDesiredInputSize();
  } else
    setDefaultDesiredInputSize();

    window_size = Size(240,135);
}

// ---------------------------------------------------------------------

void CameraThread::setDefaultDesiredInputSize() {
  desired_input_size.width = 1280;
  desired_input_size.height = 720;
}

// ---------------------------------------------------------------------

// Q_DECL_OVERRIDE produces an error on OS X 10.11 / Qt 5.6: 
void CameraThread::run() Q_DECL_OVERRIDE {

    QString result;

    time_duration td, td1, td2;
    ptime nextFrameTimestamp, currentFrameTimestamp;
    ptime initialLoopTimestamp, processingDoneTimestamp, finalLoopTimestamp;
    // int delayFound = 0;

    // initialize capture on default source
    VideoCapture capture(idx);
    if (!capture.isOpened()) {
      emit errorMessage(QString("Warning: Failed to initialize camera %1.")
                                .arg(idx));
      return;
    }

#if defined(Q_OS_LINUX)
    qDebug() << "Camera" << idx
	     << ": Trying to set input size to" << desired_input_size.width 
	     << "x" << desired_input_size.height;
    QString v4l2 = QString("/usr/bin/v4l2-ctl -d /dev/video%1 -v width=%2,height=%3")
      .arg(idx).arg(desired_input_size.width).arg(desired_input_size.height);
    int ret = system(v4l2.toStdString().c_str());
    qDebug() << "Command [" << v4l2 << "] returned" << ret; 

    capture.set(CV_CAP_PROP_FRAME_WIDTH, desired_input_size.width);
    capture.set(CV_CAP_PROP_FRAME_HEIGHT, desired_input_size.height);
#endif

    // Get the properties from the camera
    input_size.width =  capture.get(CV_CAP_PROP_FRAME_WIDTH);
    input_size.height = capture.get(CV_CAP_PROP_FRAME_HEIGHT);

    // print camera frame size
    qDebug() << "Camera" << idx
             << ": Input size: width:" << input_size.width 
	     << "height:" << input_size.height;
    emit cameraInfo(idx, input_size.width, input_size.height);

    outdir = "";
    filename = QString("capture%1.avi").arg(idx);

    record_video = false;

#if defined(Q_OS_WIN)
    fourcc = -1;
#else
    fourcc = CV_FOURCC('m','p','4','v');
#endif

    // Note: These need to match the default values in AvRecorder::AvRecorder():
    framerate = 25;
    output_size = Size(640,360);

    // initialize initial timestamps
    nextFrameTimestamp = microsec_clock::local_time();
    currentFrameTimestamp = nextFrameTimestamp;
    td = (currentFrameTimestamp - nextFrameTimestamp);

    QLinkedList<time_duration> tdlist;

    stopLoop = false;
    is_active = true;

    double avgload = 0.0;
    size_t nframe = 0;
    for (;;) {

      if (stopLoop) {
	qDebug() << "Camera" << idx << "stopping";
	break;
      }

      // Happens when Stop was pressed:
      if (!record_video && video.isOpened())
	  video.release();
      
      // determine time at start of loop
      initialLoopTimestamp = microsec_clock::local_time();
            
      Mat frame;
      
      capture >> frame;
      nframe++;
      
      if (is_active) {
	  was_active = true;

	  if (frame.cols && frame.rows) {
	  
	      if (output_size.width != 0)
		  resizeAR(frame, output_size);
	  
	      QDateTime datetime = QDateTime::currentDateTime();
	      rectangle(frame, Point(2,frame.rows-22), Point(300, frame.rows-8),
			Scalar(0,0,0), CV_FILLED);
	      putText(frame, datetime.toString().toStdString().c_str(),
		      Point(10,frame.rows-10), FONT_HERSHEY_PLAIN, 1.0,
		      Scalar(255,255,255));
	  
	      // Save frame to video
	      if (record_video && video.isOpened())
		  video << frame;

	      Mat window;
	      resize(frame, window, window_size);
	      putText(window, QString::number(nframe).toStdString().c_str(),
		      Point(10, 20), FONT_HERSHEY_PLAIN, 1.5,
		      Scalar(0,0,255), 2);
	      putText(window, QString::number(avgload, 'f', 2).toStdString().c_str(),
		      Point(window.cols-60, 20), FONT_HERSHEY_PLAIN, 1.5,
		      Scalar(0,0,255), 2);
	      if (avgload>1.0)
		  putText(window, "CPU OVERLOAD",
			  Point(0,80), FONT_HERSHEY_PLAIN, 1.9,
			  Scalar(0,0,255), 2);
	      QImage qimg = Mat2QImage(window);
	      emit qimgReady(idx, qimg);
	  
	  } else
	      qDebug() << "Camera" << idx << ": Skipped frame";
      } else if (was_active) {
	  was_active = false;
	  QImage qimg = Mat2QImage(Mat::zeros(window_size, CV_8UC3));
	  emit qimgReady(idx, qimg);
      }

      //write previous and current frame timestamp to console
      //qDebug() << nextFrameTimestamp << " " << currentFrameTimestamp << " ";

      // determine time when all processing done
      processingDoneTimestamp = microsec_clock::local_time();

      // wait for X microseconds until 1second/framerate time has passed after previous frame write
      while(td.total_microseconds() < 1000000/framerate){
	  //determine current elapsed time
	  currentFrameTimestamp = microsec_clock::local_time();
	  td = (currentFrameTimestamp - nextFrameTimestamp);
      }

      // add 1second/framerate time for next loop pause
      nextFrameTimestamp = nextFrameTimestamp + microsec(1000000/framerate);
      
      // reset time_duration so while loop engages
      td = (currentFrameTimestamp - nextFrameTimestamp);
      
      //determine and print out delay in ms, should be less than 1000/FPS
      //occasionally, if delay is larger than said value, correction will occur
      //if delay is consistently larger than said value, then CPU is not powerful
      // enough to capture/decompress/record/compress that fast.
      finalLoopTimestamp = microsec_clock::local_time();
      td1 = (processingDoneTimestamp - initialLoopTimestamp);
      td2 = (finalLoopTimestamp - initialLoopTimestamp);

      tdlist << td1;
      size_t tdlistsize = tdlist.size();
      if (tdlistsize>100)
	  tdlist.removeFirst();
      long total_td = 0;
      QLinkedList<time_duration>::const_iterator it;
      for (it = tdlist.constBegin(); it != tdlist.constEnd(); ++it)
	  total_td += it->total_microseconds();
      avgload = total_td*framerate/(tdlistsize*1000000.0);
      /*
      QDebug dbg = qDebug(); dbg.noquote();
      dbg << idx << ": Processing:"
	  << QString("%1").arg(td1.total_microseconds(), 6, 10, QChar(' '))
	  << "us. Final:"
	  << QString("%1").arg(td2.total_microseconds(), 6, 10, QChar(' '))
	  << "us. Target:" << 1000000/framerate << "us. ("
	  << td2.total_microseconds()*framerate/10000.0 << "% ) Load:"
	  << avgload << tdlistsize;
      */
      
    } // for (;;) // td2.total_milliseconds()

    emit resultReady(result);
}

// ---------------------------------------------------------------------

void CameraThread::resizeAR(Mat &frame, Size osize) {

    float o_aspect_ratio = float(osize.width)/float(osize.height);
    float f_aspect_ratio = float(frame.cols)/float(frame.rows);

    //qDebug() << o_aspect_ratio << f_aspect_ratio << fabs(f_aspect_ratio-o_aspect_ratio);

    if (fabs(f_aspect_ratio-o_aspect_ratio)<0.01) {
	resize(frame, frame, osize);
	return;
    }

    Mat newframe = Mat::zeros(osize, CV_8UC3);
    size_t roi_width = size_t(f_aspect_ratio*osize.height);
    size_t roi_displacement = (osize.width-roi_width)/2;
    Mat roi(newframe, Rect(roi_displacement, 0, roi_width, osize.height));
    resize(frame, roi, roi.size());
    newframe.copyTo(frame);

    //qDebug() << roi.cols << roi.rows;
    return;
}

// ---------------------------------------------------------------------

void CameraThread::setOutputDirectory(const QString &d) {
    outdir = d+"/";
}

// ---------------------------------------------------------------------

void CameraThread::onStateChanged(QMediaRecorder::State state) {
    switch (state) {
    case QMediaRecorder::RecordingState:
        if (!is_active) {
            record_video = false;
            break;
        }
        if (!video.isOpened()) {
	    qDebug() << QString("CameraThread::onStateChanged(): initializing "
				"VideoWriter for camera %1").arg(idx);
	    video.open(QString(outdir+filename).toStdString(), fourcc, framerate,
                       (output_size.width ? output_size : input_size));
        }
        if (!video.isOpened()) {
            emit errorMessage(QString("ERROR: Failed to initialize camera %1")
                                      .arg(idx));
        } else {
	    qDebug() << QString("CameraThread::onStateChanged(): initialization "
				"ready for camera %1").arg(idx);
            record_video = true;
        }
        break;
    case QMediaRecorder::PausedState:
        record_video = false;
        break;
    case QMediaRecorder::StoppedState:
        record_video = false;
        break;
    }
}

// ---------------------------------------------------------------------

QImage CameraThread::Mat2QImage(cv::Mat const& src) {
     cv::Mat temp;
     cvtColor(src, temp,CV_BGR2RGB);
     QImage dest((const uchar *) temp.data, temp.cols, temp.rows, 
		 temp.step, QImage::Format_RGB888);
     dest.bits(); // enforce deep copy, see documentation
     return dest;
}

// ---------------------------------------------------------------------

void CameraThread::setCameraOutput(QString wxh) {
    qDebug() << "CameraThread::setCameraOutput(): " << wxh;
    if (wxh == "Original") {
        output_size = Size(0,0);
    } else {
        QStringList wh = wxh.split("x");
        if (wh.length()==2) {
            output_size = Size(wh.at(0).toInt(), wh.at(1).toInt());
        }
    }
}

// ---------------------------------------------------------------------

void CameraThread::setCameraFramerate(QString fps) {
    qDebug() << "CameraThread::setCameraFramerate(): " << fps;
    framerate  = fps.toInt();
}

// ---------------------------------------------------------------------

void CameraThread::breakLoop() {
    stopLoop = true;
}

// ---------------------------------------------------------------------

void CameraThread::setCameraPower(int i, int state) {
    if (i == idx) {
        qDebug() << "Camera" << idx << "power now" << state;
        is_active = state;
    }
}

// ---------------------------------------------------------------------

// Local Variables:
// c-basic-offset: 4
// End:
