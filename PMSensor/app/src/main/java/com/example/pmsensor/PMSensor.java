package com.example.pmsensor;

public class PMSensor {
    public float PM2_5;
    public float PM10;
    public String Id;
    public float humidity;
    public float humidityRaw;
    public float temperature;
    public float temperatureRaw;
    public float uv;
    public float lightQuantity;
    public float atmosphericPressure;
    public String measureTime;

    public PMSensor() {
    }

    public PMSensor(float PM2_5, float PM10, String id, float humidity, float humidityRaw, float temperature, float temperatureRaw, float uv, float lightQuantity, float atmosphericPressure, String measureTime) {
        this.PM2_5 = PM2_5;
        this.PM10 = PM10;
        Id = id;
        this.humidity = humidity;
        this.humidityRaw = humidityRaw;
        this.temperature = temperature;
        this.temperatureRaw = temperatureRaw;
        this.uv = uv;
        this.lightQuantity = lightQuantity;
        this.atmosphericPressure = atmosphericPressure;
        this.measureTime = measureTime;
    }

    public float getPM2_5() {
        return PM2_5;
    }

    public void setPM2_5(float PM2_5) {
        this.PM2_5 = PM2_5;
    }

    public float getPM10() {
        return PM10;
    }

    public void setPM10(float PM10) {
        this.PM10 = PM10;
    }

    public String getId() {
        return Id;
    }

    public void setId(String id) {
        Id = id;
    }

    public float getHumidity() {
        return humidity;
    }

    public void setHumidity(float humidity) {
        this.humidity = humidity;
    }

    public float getHumidityRaw() {
        return humidityRaw;
    }

    public void setHumidityRaw(float humidityRaw) {
        this.humidityRaw = humidityRaw;
    }

    public float getTemperature() {
        return temperature;
    }

    public void setTemperature(float temperature) {
        this.temperature = temperature;
    }

    public float getTemperatureRaw() {
        return temperatureRaw;
    }

    public void setTemperatureRaw(float temperatureRaw) {
        this.temperatureRaw = temperatureRaw;
    }

    public float getUv() {
        return uv;
    }

    public void setUv(float uv) {
        this.uv = uv;
    }

    public float getLightQuantity() {
        return lightQuantity;
    }

    public void setLightQuantity(float lightQuantity) {
        this.lightQuantity = lightQuantity;
    }

    public float getAtmosphericPressure() {
        return atmosphericPressure;
    }

    public void setAtmosphericPressure(float atmosphericPressure) {
        this.atmosphericPressure = atmosphericPressure;
    }

    public String getMeasureTime() {
        return measureTime;
    }

    public void setMeasureTime(String measureTime) {
        this.measureTime = measureTime;
    }
}
