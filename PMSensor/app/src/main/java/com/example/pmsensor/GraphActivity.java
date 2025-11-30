package com.example.pmsensor;

import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.util.Pair;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.components.Legend;
import com.github.mikephil.charting.components.XAxis;
import com.github.mikephil.charting.components.YAxis;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.formatter.ValueFormatter;
import com.google.android.material.datepicker.MaterialDatePicker;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public class GraphActivity extends AppCompatActivity {
    private String SUPABASE_URL;
    private String SUPABASE_KEY;


    private Button datePickerButton;
    private LineChart tempLineChart, humidityLineChart, pm25LineChart, pm10LineChart, uvLineChart, lightLineChart, pressureLineChart;


    private Long startDate = null;
    private Long endDate = null;


    private interface ValueExtractor {
        float extract(PMSensor sensor);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.graph);

        SUPABASE_URL = getString(R.string.supabase_url);
        SUPABASE_KEY = getString(R.string.supabase_key);


        tempLineChart = findViewById(R.id.tempLineChart);
        humidityLineChart = findViewById(R.id.humidityLineChart);
        pm25LineChart = findViewById(R.id.pm25LineChart);
        pm10LineChart = findViewById(R.id.pm10LineChart);
        uvLineChart = findViewById(R.id.uvLineChart);
        lightLineChart = findViewById(R.id.lightLineChart);
        pressureLineChart = findViewById(R.id.pressureLineChart);
        datePickerButton = findViewById(R.id.datePickerButton);


        setupDatePicker();


        Calendar cal = Calendar.getInstance();
        endDate = cal.getTimeInMillis(); // Ma
        cal.add(Calendar.DAY_OF_YEAR, -1); // 24 órával ezelőtt
        startDate = cal.getTimeInMillis();
        updateButtonText();
        fetchAndDisplaySensorData();
    }

    private void setupDatePicker() {
        datePickerButton.setOnClickListener(v -> {
            MaterialDatePicker.Builder<Pair<Long, Long>> builder = MaterialDatePicker.Builder.dateRangePicker();
            builder.setTitleText("Válassz időszakot");

            if (startDate != null && endDate != null) {
                builder.setSelection(new Pair<>(startDate, endDate));
            }

            final MaterialDatePicker<Pair<Long, Long>> datePicker = builder.build();

            datePicker.addOnPositiveButtonClickListener(selection -> {
                startDate = selection.first;
                endDate = selection.second;

                Calendar cal = Calendar.getInstance(TimeZone.getTimeZone("UTC"));
                cal.setTimeInMillis(endDate);
                cal.set(Calendar.HOUR_OF_DAY, 23);
                cal.set(Calendar.MINUTE, 59);
                cal.set(Calendar.SECOND, 59);
                endDate = cal.getTimeInMillis();

                updateButtonText();
                fetchAndDisplaySensorData();
            });

            datePicker.show(getSupportFragmentManager(), "MATERIAL_DATE_PICKER");
        });
    }

    private void updateButtonText() {
        if (startDate == null || endDate == null) {
            datePickerButton.setText("Válassz időszakot");
            return;
        }
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy.MM.dd", Locale.getDefault());
        String startStr = sdf.format(new Date(startDate));
        String endStr = sdf.format(new Date(endDate));
        datePickerButton.setText(String.format("%s - %s", startStr, endStr));
    }


    private void fetchAndDisplaySensorData() {
        new Thread(() -> {
            try {
                SimpleDateFormat supabaseFormatter = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.GERMANY);
                supabaseFormatter.setTimeZone(TimeZone.getTimeZone("UTC"));

                String startTimeString = supabaseFormatter.format(new Date(startDate));
                String endTimeString = supabaseFormatter.format(new Date(endDate));

                OkHttpClient client = new OkHttpClient();
                ArrayList<PMSensor> sensorDataList = new ArrayList<>();
                JSONArray jsonArray = new JSONArray();
                do{
                String url = SUPABASE_URL + "/rest/v1/PMSensor?select=*"
                        + "&Measure_time=gte." + startTimeString
                        + "&Measure_time=lte." + endTimeString
                        + "&order=Measure_time.asc"
                        + "&limit=1000";

                Request request = new Request.Builder()
                        .url(url)
                        .addHeader("apikey", SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + SUPABASE_KEY)
                        .addHeader("Accept", "application/json")
                        .build();

                Log.d("Supabase", "Request URL: " + url);

                    Response response = client.newCall(request).execute();
                if (response.isSuccessful() && response.body() != null) {
                    String responseBody = response.body().string();
                     jsonArray = new JSONArray(responseBody);

                    for (int i = 0; i < jsonArray.length(); i++) {
                        JSONObject obj = jsonArray.getJSONObject(i);
                        sensorDataList.add(new PMSensor(
                                (float) obj.optDouble("PM25", 0),
                                (float) obj.optDouble("PM10", 0),
                                obj.optString("ID", ""),
                                (float) obj.optDouble("Humidity", 0),
                                (float) obj.optDouble("Humidity_raw", 0),
                                (float) obj.optDouble("Temperature", 0),
                                (float) obj.optDouble("Temperature_raw", 0),
                                (float) obj.optDouble("UV", 0),
                                (float) obj.optDouble("Light_quantity", 0),
                                (float) obj.optDouble("Atmospheric_pressure", 0),
                                obj.optString("Measure_time", "")
                        ));
                    }

                    if (!sensorDataList.isEmpty()) {

                        String lastMeasureTimeString = sensorDataList.get(sensorDataList.size() - 1).measureTime;


                        SimpleDateFormat parser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.getDefault());
                        parser.setTimeZone(TimeZone.getTimeZone("UTC"));

                        Date lastDate = parser.parse(lastMeasureTimeString);


                        startTimeString = supabaseFormatter.format(lastDate);
                    }

                } else {
                    Log.e("Supabase", "Hiba a lekérés során: " + response.code() + " " + response.message());
                    runOnUiThread(() -> Toast.makeText(GraphActivity.this, "Hiba a lekérés során: " + response.message(), Toast.LENGTH_SHORT).show());
                }
                }while (jsonArray.length() ==1000);

                runOnUiThread(() -> {
                    if (!sensorDataList.isEmpty()) {
                        Toast.makeText(GraphActivity.this, sensorDataList.size() + " adatpont betöltve.", Toast.LENGTH_SHORT).show();

                        setupChart(tempLineChart, sensorDataList, "Hőmérséklet", ContextCompat.getColor(this, R.color.chart_temperature), sensor -> sensor.temperature);
                        setupChart(humidityLineChart, sensorDataList, "Páratartalom", ContextCompat.getColor(this, R.color.chart_humidity), sensor -> sensor.humidity);
                        setupChart(pm25LineChart, sensorDataList, "PM2.5", ContextCompat.getColor(this, R.color.chart_pm25), sensor -> sensor.PM2_5);
                        setupChart(pm10LineChart, sensorDataList, "PM10", ContextCompat.getColor(this, R.color.chart_pm10), sensor -> sensor.PM10);
                        setupChart(uvLineChart, sensorDataList, "UV Index", ContextCompat.getColor(this, R.color.chart_uv), sensor -> sensor.uv);
                        setupChart(lightLineChart, sensorDataList, "Fényerősség", ContextCompat.getColor(this, R.color.chart_light), sensor -> sensor.lightQuantity);
                        setupChart(pressureLineChart, sensorDataList, "Légnyomás", ContextCompat.getColor(this, R.color.chart_pressure), sensor -> sensor.atmosphericPressure);
                    } else {
                        Toast.makeText(GraphActivity.this, "Nincsenek adatok a kiválasztott időszakban.", Toast.LENGTH_LONG).show();
                        clearAllCharts();
                    }
                });
            } catch (Exception e) {
                Log.e("Supabase", "Hiba az adatok feldolgozása közben", e);
                runOnUiThread(() -> Toast.makeText(GraphActivity.this, "Hiba: " + e.getMessage(), Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    private void clearAllCharts() {
        tempLineChart.clear();
        humidityLineChart.clear();
        pm25LineChart.clear();
        pm10LineChart.clear();
        uvLineChart.clear();
        lightLineChart.clear();
        pressureLineChart.clear();

        tempLineChart.invalidate();
        humidityLineChart.invalidate();
        pm25LineChart.invalidate();
        pm10LineChart.invalidate();
        uvLineChart.invalidate();
        lightLineChart.invalidate();
        pressureLineChart.invalidate();
    }

    private void setupChart(LineChart chart, ArrayList<PMSensor> data, String label, int color, ValueExtractor extractor) {
        if (chart == null) {
            Log.e("ChartError", "A(z) '" + label + "' diagram nincs inicializálva.");
            return;
        }

        // --- Automatikus színváltás világos/sötét módhoz ---
        int nightModeFlags = getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        boolean isDarkMode = (nightModeFlags == Configuration.UI_MODE_NIGHT_YES);

        int textColor = isDarkMode ? Color.WHITE : Color.BLACK;
        int gridColor = isDarkMode
                ? Color.argb(80, 255, 255, 255)
                : Color.argb(80, 0, 0, 0);

        chart.setBackgroundColor(Color.TRANSPARENT);

        ArrayList<Entry> entries = new ArrayList<>();
        SimpleDateFormat parser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.getDefault());
        parser.setTimeZone(TimeZone.getTimeZone("UTC"));

        for (PMSensor sensor : data) {
            if (sensor.measureTime == null || sensor.measureTime.isEmpty()) continue;
            try {
                String timeString = sensor.measureTime;
                if (timeString.length() > 19) {
                    timeString = timeString.substring(0, 19);
                }

                Date date = parser.parse(timeString);
                if (date != null) {
                    entries.add(new Entry(date.getTime(), extractor.extract(sensor)));
                }
            } catch (ParseException e) {
                Log.e("ChartData", "Dátum formázási hiba: " + sensor.measureTime, e);
            }
        }

        LineDataSet dataSet = new LineDataSet(entries, label);
        dataSet.setColor(color);
        dataSet.setLineWidth(2f);
        dataSet.setDrawCircles(false);


        dataSet.setDrawValues(false);


        dataSet.setHighLightColor(color);
        dataSet.setHighlightLineWidth(1f);
        dataSet.setDrawHorizontalHighlightIndicator(false);


        LineData lineData = new LineData(dataSet);
        chart.setData(lineData);

        // --- X és Y tengelyek ---
        XAxis xAxis = chart.getXAxis();
        xAxis.setPosition(XAxis.XAxisPosition.BOTTOM);
        xAxis.setLabelRotationAngle(-45);
        xAxis.setTextColor(textColor);
        xAxis.setGridColor(gridColor);
        xAxis.setAxisLineColor(gridColor);
        xAxis.setSpaceMin(0f);
        xAxis.setSpaceMax(0f);
        xAxis.setDrawGridLines(true);
        xAxis.setGranularity(1f);
        xAxis.setGranularityEnabled(true);
        xAxis.setDrawLabels(true);

        xAxis.setValueFormatter(new ValueFormatter() {
            private final SimpleDateFormat sdf = new SimpleDateFormat("MM.dd HH:mm", Locale.getDefault());
            {
                sdf.setTimeZone(TimeZone.getTimeZone("CEST"));
            }

            @Override
            public String getAxisLabel(float value, com.github.mikephil.charting.components.AxisBase axis) {

                return sdf.format(new Date((long) value));
            }
        });

        YAxis leftAxis = chart.getAxisLeft();
        leftAxis.setTextColor(textColor);
        leftAxis.setGridColor(gridColor);
        leftAxis.setAxisLineColor(gridColor);

        chart.getAxisRight().setEnabled(false);

        // --- Leírás és legenda ---
        chart.getDescription().setEnabled(false);
        Legend legend = chart.getLegend();
        legend.setEnabled(false);

        chart.setExtraBottomOffset(50f);

        // --- MARKER BEÁLLÍTÁSA ---
        MyMarkerView marker = new MyMarkerView(this, R.layout.custom_marker_view);
        chart.setMarker(marker);

        // --- Interakció ---
        chart.setDragEnabled(true);
        chart.setScaleEnabled(true);
        chart.setPinchZoom(true);
        chart.setDoubleTapToZoomEnabled(true);

        chart.invalidate();
    }
}
