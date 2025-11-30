package com.example.pmsensor;

import android.content.Context;
import android.util.Log;
import androidx.annotation.NonNull;
import androidx.work.Worker;
import androidx.work.WorkerParameters;
import org.json.JSONArray;
import org.json.JSONObject;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public class SensorWorker extends Worker {

    private static final String TAG = "SensorWorker";
    private final Context context;

    public SensorWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
        this.context = context;
    }

    @NonNull
    @Override
    public Result doWork() {
        Log.d(TAG, "Worker fut: adatok lekérése...");
        try {
            String supabaseUrl = context.getString(R.string.supabase_url);
            String supabaseKey = context.getString(R.string.supabase_key);

            OkHttpClient client = new OkHttpClient();
            Request request = new Request.Builder()
                    .url(supabaseUrl + "/rest/v1/PMSensor?select=*&order=Measure_time.desc&limit=1")
                    .addHeader("apikey", supabaseKey)
                    .addHeader("Authorization", "Bearer " + supabaseKey)
                    .build();

            Response response = client.newCall(request).execute();

            if (response.isSuccessful() && response.body() != null) {
                String responseBody = response.body().string();
                JSONArray jsonArray = new JSONArray(responseBody);

                if (jsonArray.length() > 0) {
                    JSONObject obj = jsonArray.getJSONObject(0);
                    float temp = (float) obj.getDouble("Temperature");
                    float humid = (float) obj.getDouble("Humidity");
                    float pm25 = (float) obj.getDouble("PM25");
                    float pm10 = (float) obj.getDouble("PM10");
                    float uv = (float) obj.getDouble("UV");

                    // Értesítések küldése az új segédosztállyal
                    NotificationHelper.showOngoingNotification(context, temp, humid, uv, pm25, pm10);
                    NotificationHelper.checkThresholdsAndAlert(context, pm25, pm10);

                    Log.d(TAG, "Adatok sikeresen frissítve.");
                    return Result.success();
                }
            }

            Log.e(TAG, "Adatlekérés sikertelen, válaszkód: " + response.code());
            return Result.retry();

        } catch (Exception e) {
            Log.e(TAG, "Hiba a Worker futása közben", e);
            return Result.failure();
        }
    }
}
