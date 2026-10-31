/*
OBS Looking Glass - Custom Dynamic Multiview Plugin
Copyright (C) 2025

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <QtTest>

#include "content-ref.hpp"

class TestContentRef : public QObject {
	Q_OBJECT

private slots:
	void mimeType() { QCOMPARE(ContentRefs::MimeType(), QString("application/x-looking-glass-content")); }

	void roundTrip()
	{
		const WidgetType types[] = {WidgetType::None,       WidgetType::Preview, WidgetType::Program,
					    WidgetType::Canvas,     WidgetType::Scene,   WidgetType::Source,
					    WidgetType::Placeholder};
		for (WidgetType t : types) {
			ContentRef in;
			in.type = t;
			in.name = QString::fromUtf8("Cam é \"1\"");
			in.uuid = "abc-123";
			ContentRef out;
			QVERIFY(ContentRefs::Decode(ContentRefs::Encode(in), &out));
			QCOMPARE(out.type, in.type);
			QCOMPARE(out.name, in.name);
			QCOMPARE(out.uuid, in.uuid);
		}
	}

	void decodeRejectsMalformed()
	{
		ContentRef out;
		QVERIFY(!ContentRefs::Decode("", &out));
		QVERIFY(!ContentRefs::Decode("not json", &out));
		QVERIFY(!ContentRefs::Decode("[1,2]", &out));
		QVERIFY(!ContentRefs::Decode("{\"name\":\"x\"}", &out));
		QVERIFY(!ContentRefs::Decode("{\"type\":5}", &out));
		QVERIFY(!ContentRefs::Decode("{\"type\":\"bogus\",\"name\":\"x\"}", &out));
		QCOMPARE(out.type, WidgetType::None);
	}

	void applySceneKeepsStyleAndEnablesTally()
	{
		WidgetConfig w;
		w.type = WidgetType::Source;
		w.sourceName = "Old";
		w.sourceUuid = "old-uuid";
		w.labelText = "Mine";
		w.labelSize = 150;
		w.safeRegion = true;

		ContentRefs::Apply(w, {WidgetType::Scene, "Scene 1", "u1"});
		QCOMPARE(w.type, WidgetType::Scene);
		QCOMPARE(w.sceneName, QString("Scene 1"));
		QCOMPARE(w.sourceUuid, QString("u1"));
		QVERIFY(w.sourceName.isEmpty());
		QVERIFY(w.showStatus);
		QCOMPARE(w.labelText, QString("Mine"));
		QCOMPARE(w.labelSize, 150);
		QVERIFY(w.safeRegion);
	}

	void applySceneToSceneKeepsTallyChoice()
	{
		WidgetConfig w;
		ContentRefs::Apply(w, {WidgetType::Scene, "A", "ua"});
		QVERIFY(w.showStatus);
		w.showStatus = false;
		ContentRefs::Apply(w, {WidgetType::Scene, "B", "ub"});
		QVERIFY(!w.showStatus);
		QCOMPARE(w.sceneName, QString("B"));
	}

	void applySource()
	{
		WidgetConfig w;
		ContentRefs::Apply(w, {WidgetType::Scene, "A", "ua"});
		ContentRefs::Apply(w, {WidgetType::Source, "Cam", "uc"});
		QCOMPARE(w.type, WidgetType::Source);
		QCOMPARE(w.sourceName, QString("Cam"));
		QCOMPARE(w.sourceUuid, QString("uc"));
		QVERIFY(w.sceneName.isEmpty());
		QVERIFY(!w.showStatus);
	}

	void applyCanvasClearsTargets()
	{
		WidgetConfig w;
		ContentRefs::Apply(w, {WidgetType::Scene, "A", "ua"});
		ContentRefs::Apply(w, {WidgetType::Canvas, "Vertical", "ignored"});
		QCOMPARE(w.type, WidgetType::Canvas);
		QCOMPARE(w.canvasName, QString("Vertical"));
		QVERIFY(w.sourceUuid.isEmpty());
		QVERIFY(w.sceneName.isEmpty());
		QVERIFY(w.sourceName.isEmpty());
		QVERIFY(!w.showStatus);

		ContentRefs::Apply(w, {WidgetType::Canvas, "", ""});
		QVERIFY(w.canvasName.isEmpty());
	}

	void applyOtherTypesClearAll()
	{
		for (WidgetType t :
		     {WidgetType::None, WidgetType::Preview, WidgetType::Program, WidgetType::Placeholder}) {
			WidgetConfig w;
			ContentRefs::Apply(w, {WidgetType::Scene, "A", "ua"});
			w.canvasName = "c";
			ContentRefs::Apply(w, {t, "ignored", "ignored"});
			QCOMPARE(w.type, t);
			QVERIFY(w.sceneName.isEmpty());
			QVERIFY(w.sourceName.isEmpty());
			QVERIFY(w.sourceUuid.isEmpty());
			QVERIFY(w.canvasName.isEmpty());
			QVERIFY(!w.showStatus);
		}
	}

	void fromWidget()
	{
		WidgetConfig w;
		ContentRefs::Apply(w, {WidgetType::Scene, "A", "ua"});
		ContentRef r = ContentRefs::FromWidget(w);
		QCOMPARE(r.type, WidgetType::Scene);
		QCOMPARE(r.name, QString("A"));
		QCOMPARE(r.uuid, QString("ua"));

		ContentRefs::Apply(w, {WidgetType::Source, "S", "us"});
		r = ContentRefs::FromWidget(w);
		QCOMPARE(r.name, QString("S"));
		QCOMPARE(r.uuid, QString("us"));

		ContentRefs::Apply(w, {WidgetType::Canvas, "V", ""});
		r = ContentRefs::FromWidget(w);
		QCOMPARE(r.type, WidgetType::Canvas);
		QCOMPARE(r.name, QString("V"));

		ContentRefs::Apply(w, {WidgetType::Program, "", ""});
		r = ContentRefs::FromWidget(w);
		QCOMPARE(r.type, WidgetType::Program);
		QVERIFY(r.name.isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestContentRef)
#include "test-content-ref.moc"
