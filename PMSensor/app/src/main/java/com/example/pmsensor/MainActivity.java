package com.example.pmsensor;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

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

    // Supabase adatok

    private String SUPABASE_URL;
    private String SUPABASE_KEY;

    private static final int LOCATION_PERMISSION_REQUEST_CODE = 101;
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

        // 🔹 Supabase-ből lekérés
        loadLastSensorData();
        /*
        if (ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        ) {
            // You have the permission, proceed with fine location access
        } else {
            // Request both permissions from the user
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{
                            Manifest.permission.ACCESS_FINE_LOCATION,
                            Manifest.permission.ACCESS_COARSE_LOCATION
                    },
                    LOCATION_PERMISSION_REQUEST_CODE
            );
        }*/
    }

    private void loadLastSensorData() {
        new Thread(() -> {
            try {
                OkHttpClient client = new OkHttpClient();

                // Supabase query (legfrissebb rekord a PMSensor táblából)
                Request request = new Request.Builder()
                        .url(SUPABASE_URL + "/rest/v1/PMSensor?select=*&order=Measure_time.desc&limit=1")
                        .addHeader("apikey", SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + SUPABASE_KEY)
                        .addHeader("Accept", "application/json")  // 🔹 fontos!
                        .build();


                Response response = client.newCall(request).execute();
                if (response.isSuccessful()) {
                    String responseBody = response.body().string();
                    JSONArray jsonArray = new JSONArray(responseBody);

                    if (jsonArray.length() > 0) {
                        JSONObject obj = jsonArray.getJSONObject(0);

                        // 🔹 JSON értékek
                        final String time = obj.getString("Measure_time");
                        final float temperature = (float) obj.getDouble("Temperature");
                        final float humidity = (float) obj.getDouble("Humidity");
                        final float pressure = (float) obj.getDouble("Atmospheric_pressure");
                        final float light = (float) obj.getDouble("Light_quantity");
                        final float uv = (float) obj.getDouble("UV");
                        final float pm25 = (float) obj.getDouble("PM25");
                        final float pm10 = (float) obj.getDouble("PM10");

// 🔹 Dátum formázás
                        String formatted;
                        try {
                            SimpleDateFormat parser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.getDefault());
                            parser.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
                            Date date = parser.parse(time);

                            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault());
                            formatted = sdf.format(date);
                        } catch (Exception e) {
                            Log.w("Supabase", "Date parse failed, raw value: " + time);
                            formatted = time;
                        }
                        final String finalTime = formatted; // így final

// 🔹 UI frissítés
                        runOnUiThread(() -> {
                            timeView.setText(finalTime);
                            temperatureView.setText(temperature + " °C");
                            humidityView.setText(humidity + " %");
                            atmosphericPressureView.setText(pressure + " hPa");
                            lightQuantityView.setText(light + " Lux");
                            uvView.setText(uv + " uv");
                            pm25View.setText("PM2.5: " + pm25 + " µg/m3");
                            pm10View.setText("PM10: " + pm10 + " µg/m3");
                        });

                    }
                } else {
                    Log.e("Supabase", "Request failed: " + response.code());
                    runOnUiThread(() ->
                            Toast.makeText(MainActivity.this, "Failed to load sensor data.", Toast.LENGTH_SHORT).show()
                    );
                }
            } catch (Exception e) {
                Log.e("Supabase", "Error fetching data", e);
                runOnUiThread(() ->
                        Toast.makeText(MainActivity.this, "Error: " + e.getMessage(), Toast.LENGTH_SHORT).show()
                );
            }
        }).start();
    }
}
