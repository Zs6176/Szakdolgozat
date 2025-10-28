package com.example.pmsensor;

import android.content.Context;
import android.widget.TextView;

// Helyes import hozzáadása
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.highlight.Highlight;
import com.github.mikephil.charting.utils.MPPointF;

import java.text.SimpleDateFormat;
import java.util.Locale;
import java.util.TimeZone;

public class MyMarkerView extends com.github.mikephil.charting.components.MarkerView {

    private final TextView tvContent;

    public MyMarkerView(Context context, int layoutResource) {
        super(context, layoutResource);
        tvContent = findViewById(R.id.tvContent);
    }

    // Ezt a metódust hívja meg a diagram minden alkalommal, amikor a MarkerView-t újra kell rajzolni
    // Ezt a metódust hívja meg a diagram minden alkalommal, amikor a MarkerView-t újra kell rajzolni
    @Override
    public void refreshContent(Entry e, Highlight highlight) {
        // Kinyerjük az Y értéket (a mért adat, pl. hőmérséklet)
        float yValue = e.getY();

        // Kinyerjük az X értéket (ami egy timestamp)
        long xTimestamp = (long) e.getX();

        // Létrehozunk egy dátumformázót a timestamp olvashatóvá tételéhez
        // Olyan formátumot válasszunk, ami jól kifér a kis ablakba.
        SimpleDateFormat sdf = new SimpleDateFormat("MM.dd HH:mm", Locale.getDefault());
        sdf.setTimeZone(TimeZone.getTimeZone("CEST"));
        String formattedDate = sdf.format(new java.util.Date(xTimestamp));

        // Összefűzzük a két értéket egyetlen stringbe
        String textToShow = String.format(Locale.getDefault(), "Érték: %.1f\nIdő: %s", yValue, formattedDate);

        // Beállítjuk az elkészült szöveget a TextView-ban
        tvContent.setText(textToShow);

        super.refreshContent(e, highlight);
    }


    @Override
    public MPPointF getOffset() {
        // Ez a metódus határozza meg, hova kerüljön a marker a ponthoz képest.
        // -getWidth() / 2 -> vízszintesen középre
        // -getHeight()     -> a pont fölé
        return new MPPointF(-(getWidth() / 2f), -getHeight());
    }
}
