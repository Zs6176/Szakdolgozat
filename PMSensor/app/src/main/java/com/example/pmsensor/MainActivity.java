package com.example.pmsensor;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.work.ExistingPeriodicWorkPolicy;
import androidx.work.PeriodicWorkRequest;
import androidx.work.WorkManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;
import java.util.concurrent.TimeUnit;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {
    private TextView timeView;
    private TextView temperatureView;
    private TextView humidityView;
    private TextView atmosphericPressureView;
    private TextView lightQuantityView;
    private TextView uvView;
    private TextView pm25View;
    private TextView pm10View;
    private Button graphButton;

    private String SUPABASE_URL;
    private String SUPABASE_KEY;

    private static final String SENSOR_WORK_TAG = "SensorDataFetchWork";

    private final ActivityResultLauncher<String> requestPermissionLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestPermission(), isGranted -> {
                if (isGranted) {
                    Log.d("MainActivity", "Értesítési engedély megadva.");

                    setupPeriodicWork();
                } else {
                    Log.w("MainActivity", "Értesítési engedély megtagadva.");
                    Toast.makeText(this, "Értesítések nélkül a háttérfrissítések nem fognak működni.", Toast.LENGTH_LONG).show();
                }
            });


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);

        SUPABASE_URL = getString(R.string.supabase_url);
        SUPABASE_KEY = getString(R.string.supabase_key);

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });

        timeView = findViewById(R.id.measureTime);
        temperatureView = findViewById(R.id.temperature);
        humidityView = findViewById(R.id.humidity);
        atmosphericPressureView = findViewById(R.id.atmosphericPressure);
        lightQuantityView = findViewById(R.id.lightQuantity);
        uvView = findViewById(R.id.uv);
        pm25View = findViewById(R.id.pm25);
        pm10View = findViewById(R.id.pm10);
        graphButton = findViewById(R.id.button);

        loadLastSensorData();

        graphButton.setOnClickListener(v -> openGraphActivity());

        NotificationHelper.createNotificationChannels(this);

        askNotificationPermissionAndStartWork();
    }


    @Override
    protected void onResume() {
        super.onResume();
        loadLastSensorData();
    }

    private void loadLastSensorData() {
        new Thread(() -> {
            try {
                OkHttpClient client = new OkHttpClient();
                Request request = new Request.Builder()
                        .url(SUPABASE_URL + "/rest/v1/PMSensor?select=*&order=Measure_time.desc&limit=1")
                        .addHeader("apikey", SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + SUPABASE_KEY)
                        .addHeader("Accept", "application/json")
                        .build();

                Response response = client.newCall(request).execute();
                if (response.isSuccessful() && response.body() != null) {
                    String responseBody = response.body().string();
                    JSONArray jsonArray = new JSONArray(responseBody);

                    if (jsonArray.length() > 0) {
                        JSONObject obj = jsonArray.getJSONObject(0);

                        final String time = obj.getString("Measure_time");
                        final float temperature = (float) obj.getDouble("Temperature");
                        final float humidity = (float) obj.getDouble("Humidity");
                        final float pressure = (float) obj.getDouble("Atmospheric_pressure");
                        final float light = (float) obj.getDouble("Light_quantity");
                        final float uv = (float) obj.getDouble("UV");
                        final float pm25 = (float) obj.getDouble("PM25");
                        final float pm10 = (float) obj.getDouble("PM10");

                        String formattedTime = formatDisplayDate(time);

                        runOnUiThread(() -> {
                            timeView.setText(formattedTime);
                            temperatureView.setText(String.format(Locale.getDefault(), "%.1f °C", temperature));
                            humidityView.setText(String.format(Locale.getDefault(), "%.0f %%", humidity));
                            atmosphericPressureView.setText(String.format(Locale.getDefault(), "%.0f hPa", pressure));
                            lightQuantityView.setText(String.format(Locale.getDefault(), "%.0f Lux", light));
                            uvView.setText(String.format(Locale.getDefault(), "%.1f", uv));
                            pm25View.setText(String.format(Locale.getDefault(), "PM2.5: %.1f µg/m³", pm25));
                            pm10View.setText(String.format(Locale.getDefault(), "PM10: %.1f µg/m³", pm10));
                        });
                    }
                } else {
                    Log.e("Supabase", "Request failed: " + response.code());
                }
            } catch (Exception e) {
                Log.e("Supabase", "Error fetching data", e);
            }
        }).start();
    }


    private String formatDisplayDate(String isoString) {
        try {

            java.time.LocalDateTime localDateTime = java.time.LocalDateTime.parse(
                    isoString.substring(0, 19)
            );


            java.time.ZoneId hungarianZoneId = java.time.ZoneId.of("Europe/Budapest");
            java.time.ZonedDateTime zonedDateTime = localDateTime.atZone(hungarianZoneId);

            java.time.format.DateTimeFormatter displayFormatter =
                    java.time.format.DateTimeFormatter.ofPattern("yyyy.MM.dd HH:mm", Locale.getDefault());


            return zonedDateTime.format(displayFormatter);

        } catch (Exception e) {
            Log.e("DateParser", "Dátumformázás sikertelen, a nyers string kerül megjelenítésre.", e);
            return isoString;
        }
    }



    private void openGraphActivity() {
        Intent intent = new Intent(this, GraphActivity.class);
        startActivity(intent);
    }

    private void askNotificationPermissionAndStartWork() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
                setupPeriodicWork();
            } else {
                requestPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS);
            }
        } else {

            setupPeriodicWork();
        }
    }

    private void setupPeriodicWork() {
        PeriodicWorkRequest periodicWorkRequest =
                new PeriodicWorkRequest.Builder(SensorWorker.class, 5, TimeUnit.MINUTES)
                        .addTag(SENSOR_WORK_TAG)
                        .build();


        WorkManager.getInstance(this).enqueueUniquePeriodicWork(
                SENSOR_WORK_TAG,
                ExistingPeriodicWorkPolicy.REPLACE,
                periodicWorkRequest
        );

        Log.d("MainActivity", "SensorWorker beütemezve 15 percenkénti futásra.");
        Toast.makeText(this, "Háttérfrissítés beállítva.", Toast.LENGTH_SHORT).show();
    }
}
