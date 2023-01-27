package com.hikvision.drmdemo;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;

import com.hikvision.jni.MyTest;

public class MainActivity extends AppCompatActivity {
    MyTest myTest;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        myTest = new MyTest();
        myTest.native_helloWorld();
    }
}