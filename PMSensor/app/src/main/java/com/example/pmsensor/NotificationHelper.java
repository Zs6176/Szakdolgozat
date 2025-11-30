package com.example.pmsensor;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;

import java.util.Locale;

public class NotificationHelper {

    private static final String TAG = "NotificationHelper";
    private static final String ONGOING_CHANNEL_ID = "SensorOngoingChannel";
    public static final int ONGOING_NOTIFICATION_ID = 1;

    private static final String ALERT_CHANNEL_ID = "SensorAlertChannel";
    private static final int PM25_ALERT_ID = 2;
    private static final int PM10_ALERT_ID = 3;

    // Küszöbértékek
    private static final float PM25_THRESHOLD = 35.0f;
    private static final float PM10_THRESHOLD = 50.0f;

    // Csatornák létrehozása
    public static void createNotificationChannels(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager manager = context.getSystemService(NotificationManager.class);
            if (manager == null) return;

            NotificationChannel ongoingChannel = new NotificationChannel(
                    ONGOING_CHANNEL_ID, "Folyamatos Adat", NotificationManager.IMPORTANCE_LOW);
            ongoingChannel.setDescription("Folyamatosan mutatja az aktuális szenzoradatokat");
            ongoingChannel.setSound(null, null);
            manager.createNotificationChannel(ongoingChannel);

            NotificationChannel alertChannel = new NotificationChannel(
                    ALERT_CHANNEL_ID, "Riasztások", NotificationManager.IMPORTANCE_HIGH);
            alertChannel.setDescription("Figyelmeztetések magas szenzorértékek esetén");
            manager.createNotificationChannel(alertChannel);
        }
    }

    public static void showOngoingNotification(Context context, float temp, float humid, float uv, float pm25, float pm10) {

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "Értesítés nem küldhető el, mert a POST_NOTIFICATIONS engedély hiányzik.");
            return;
        }


        String contentText = String.format(Locale.getDefault(), "Hőm: %.1f°C, Páratart.: %.0f%%, PM2.5: %.1f", temp, humid, pm25);
        String bigText = String.format(Locale.getDefault(), "Hőmérséklet: %.1f°C\nPáratartalom: %.0f%%\nUV Index: %.1f\nPM2.5: %.1f µg/m³\nPM10: %.1f µg/m³",
                temp, humid, uv, pm25, pm10);

        Intent intent = new Intent(context, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(context, 0, intent, PendingIntent.FLAG_IMMUTABLE);

        Notification notification = new NotificationCompat.Builder(context, ONGOING_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle("Szenzor Adatok")
                .setContentText(contentText)
                .setStyle(new NotificationCompat.BigTextStyle().bigText(bigText))
                .setContentIntent(pendingIntent)
                .build();

        NotificationManagerCompat.from(context).notify(ONGOING_NOTIFICATION_ID, notification);
    }


    public static void checkThresholdsAndAlert(Context context, float pm25, float pm10) {
        if (pm25 > PM25_THRESHOLD) {
            sendAlert(context, PM25_ALERT_ID, "Magas PM2.5 érték!",
                    String.format(Locale.getDefault(), "A levegő minősége rossz. Aktuális érték: %.1f µg/m³.", pm25));
        }
        if (pm10 > PM10_THRESHOLD) {
            sendAlert(context, PM10_ALERT_ID, "Magas PM10 érték!",
                    String.format(Locale.getDefault(), "A levegő minősége rossz. Aktuális érték: %.1f µg/m³.", pm10));
        }
    }


    private static void sendAlert(Context context, int notificationId, String title, String content) {

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "Riasztás nem küldhető el, mert a POST_NOTIFICATIONS engedély hiányzik.");
            return;
        }


        Intent intent = new Intent(context, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(context, notificationId, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Notification notification = new NotificationCompat.Builder(context, ALERT_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(title)
                .setContentText(content)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setContentIntent(pendingIntent)
                .setAutoCancel(true)
                .build();

        NotificationManagerCompat.from(context).notify(notificationId, notification);
    }
}
