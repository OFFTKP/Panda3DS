#include <QPushButton>
#include <QVBoxLayout>

#include "panda_qt/main_window.hpp"
#include "panda_qt/shader_editor.hpp"
#include <qboxlayout.h>

using namespace Zep;

ShaderEditorWindow::ShaderEditorWindow(Emulator* emulator, QWidget* parent, const std::string& filename, const std::string& initialText)
	: QDialog(parent), emulator(emulator), zepWidget(this, qApp->applicationDirPath().toStdString(), 8) {
	resize(1000, 1000);

	// Register our extensions
	ZepRegressExCommand::Register(zepWidget.GetEditor());
	ZepReplExCommand::Register(zepWidget.GetEditor(), &replProvider);

	// Default to standard mode instead of vim mode, initialize text box
	zepWidget.GetEditor().InitWithText(filename, initialText);
	zepWidget.GetEditor().SetGlobalMode(Zep::ZepMode_Standard::StaticName());

	// Layout for widgets
	QVBoxLayout* mainLayout = new QVBoxLayout();
	setLayout(mainLayout);

	QPushButton* button = new QPushButton(tr("Reload shader"), this);
	button->setFixedSize(100, 20);

	// When the Load Script button is pressed, send the current text to the MainWindow, which will upload it to the emulator's lua object
	connect(button, &QPushButton::pressed, this, [this]() {
		if (parentWidget()) {
			auto buffer = zepWidget.GetEditor().GetMRUBuffer();
			const std::string text = buffer->GetBufferText(buffer->Begin(), buffer->End());

			static_cast<MainWindow*>(parentWidget())->reloadShader(text);
		} else {
			// This should be unreachable, only here for safety purposes
			printf("Text editor does not have any parent widget, click doesn't work :(\n");
		}
	});

	mainLayout->addWidget(button);
	mainLayout->addWidget(&zepWidget);

	QImage* d0 = new QImage(256, 1, QImage::Format_RGB888);
	d0->fill(QColor(0, 0, 0));

	QImage* d1 = new QImage(256, 1, QImage::Format_RGB888);
	d1->fill(QColor(0, 0, 0));

	QImage* sp = new QImage(256, 8, QImage::Format_RGB888);
	sp->fill(QColor(0, 0, 0));

	QImage* fr = new QImage(256, 1, QImage::Format_RGB888);
	fr->fill(QColor(0, 0, 0));

	QImage* r = new QImage(256, 3, QImage::Format_RGB888);
	r->fill(QColor(0, 0, 0));

	QImage* dist = new QImage(256, 8, QImage::Format_RGB888);
	dist->fill(QColor(0, 0, 0));

	QLabel* d0Label = new QLabel(this);
	d0Label->setPixmap(QPixmap::fromImage(*d0));

	QLabel* d1Label = new QLabel(this);
	d1Label->setPixmap(QPixmap::fromImage(*d1));

	QLabel* spLabel = new QLabel(this);
	spLabel->setPixmap(QPixmap::fromImage(*sp));

	QLabel* frLabel = new QLabel(this);
	frLabel->setPixmap(QPixmap::fromImage(*fr));

	QLabel* rLabel = new QLabel(this);
	rLabel->setPixmap(QPixmap::fromImage(*r));

	QLabel* distLabel = new QLabel(this);
	distLabel->setPixmap(QPixmap::fromImage(*dist));

	QVBoxLayout* combiner1 = new QVBoxLayout();
	combiner1->addWidget(new QLabel("D0"));
	combiner1->addWidget(d0Label);
	combiner1->setAlignment(Qt::AlignTop);

	QVBoxLayout* combiner2 = new QVBoxLayout();
	combiner2->addWidget(new QLabel("D1"));
	combiner2->addWidget(d1Label);
	combiner2->setAlignment(Qt::AlignTop);

	QVBoxLayout* combiner3 = new QVBoxLayout();
	combiner3->addWidget(new QLabel("Spotlight attenuation"));
	combiner3->addWidget(spLabel);
	combiner3->setAlignment(Qt::AlignTop);

	QVBoxLayout* combiner4 = new QVBoxLayout();
	combiner4->addWidget(new QLabel("Frusnel"));
	combiner4->addWidget(frLabel);
	combiner4->setAlignment(Qt::AlignTop);

	QVBoxLayout* combiner5 = new QVBoxLayout();
	combiner5->addWidget(new QLabel("Reflection color"));
	combiner5->addWidget(rLabel);
	combiner5->setAlignment(Qt::AlignTop);

	QVBoxLayout* combiner6 = new QVBoxLayout();
	combiner6->addWidget(new QLabel("Distance attenuation"));
	combiner6->addWidget(distLabel);
	combiner6->setAlignment(Qt::AlignTop);

	QGridLayout* combinerLayout = new QGridLayout();
	combinerLayout->addLayout(combiner1, 0, 0);
	combinerLayout->addLayout(combiner2, 0, 1);
	combinerLayout->addLayout(combiner3, 0, 2);
	combinerLayout->addLayout(combiner4, 1, 0);
	combinerLayout->addLayout(combiner5, 1, 1);
	combinerLayout->addLayout(combiner6, 1, 2);

	QWidget* combinerWidget = new QWidget(this);
	combinerWidget->setLayout(combinerLayout);
	combinerWidget->setFixedSize(800, 200);

	mainLayout->addWidget(combinerWidget);

	float* floats = new float[256 * 24];
	for (int i = 0; i < 256 * 24; i++) {
		floats[i] = 0.0f;
	}
	emulator->getGPU().setFloats(floats);


	QTimer* timer = new QTimer(this);
	timer->setInterval(100);
	
	auto callback = [this, emulator, d0, d1, sp, fr, r, dist, d0Label, d1Label, spLabel, frLabel, rLabel, distLabel]() {
		auto& gpu = emulator->getGPU();

		for (int y = 0; y < 24; y++) {
			for (int x = 0; x < 256; x++) {
				float f = gpu.floats[x + y * 256];
				f = 255.0f * f;
				switch (y) {
					case 0:
						d0->setPixelColor(x, 0, QColor(f, f, f));
						break;
					case 1:
						d1->setPixelColor(x, 0, QColor(f, f, f));
						break;
					case 3:
						fr->setPixelColor(x, 0, QColor(f, f, f));
						break;
					case 6:
						r->setPixelColor(x, 0, QColor(f, f, f));
						break;
					case 5:
						r->setPixelColor(x, 1, QColor(f, f, f));
						break;
					case 4:
						r->setPixelColor(x, 2, QColor(f, f, f));
						break;
					case 8 ... 15:
						sp->setPixelColor(x, y - 8, QColor(f, f, f));
						break;
					case 16 ... 23:
						dist->setPixelColor(x, y - 16, QColor(f, f, f));
						break;
				}
			}
		}

		d0Label->setPixmap(QPixmap::fromImage(*d0));
		d1Label->setPixmap(QPixmap::fromImage(*d1));
		spLabel->setPixmap(QPixmap::fromImage(*sp));
		frLabel->setPixmap(QPixmap::fromImage(*fr));
		rLabel->setPixmap(QPixmap::fromImage(*r));
		distLabel->setPixmap(QPixmap::fromImage(*dist));
	};

	connect(timer, &QTimer::timeout, this, callback);
	timer->start();
}

void ShaderEditorWindow::setEnable(bool enable) {
	supported = enable;

	if (enable) {
		setDisabled(false);
	} else {
		setDisabled(true);
		setText("Shader editor window is not available for this renderer backend");
	}
}
