package com.nht.service;

import static android.net.nsd.NsdManager.*;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.util.Log;

public class ESP32CamDiscoverer {
    private static final String TAG = "ESP32CamDiscoverer";
    private static final String SERVICE_TYPE = "_http.[...](asc_slot://start-slot-25)_tcp."; // Matches ESP32 setup
    private static final String TARGET_SERVICE_NAME = "myeye";

    private NsdManager mNsdManager;
    private DiscoveryListener mDiscoveryListener;
    private OnESP32FoundListener mListener;

    public interface OnESP32FoundListener {
        void onDeviceFound(String ipAddress, int port);
    }

    public ESP32CamDiscoverer(Context context, OnESP32FoundListener listener) {
        mNsdManager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
        mListener = listener;
    }

    public void startDiscovery() {
        initializeDiscoveryListener();
        mNsdManager.discoverServices(SERVICE_TYPE, PROTOCOL_DNS_SD, mDiscoveryListener);
    }

    public void stopDiscovery() {
        if (mNsdManager != null && mDiscoveryListener != null) {
            try {
                mNsdManager.stopServiceDiscovery(mDiscoveryListener);
            } catch (IllegalArgumentException e) {
                Log.e(TAG, "Discovery was already stopped or not started.");
            }
        }
    }

    private void initializeDiscoveryListener() {
        mDiscoveryListener = new DiscoveryListener() {
            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery failed to start: Error code " + errorCode);
                stopDiscovery();
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery failed to stop: Error code " + errorCode);
                mNsdManager.stopServiceDiscovery(this);
            }

            @Override
            public void onDiscoveryStarted(String serviceType) {
                Log.d(TAG, "Service discovery started");
            }

            @Override
            public void onDiscoveryStopped(String s) {
                Log.d(TAG, "Service discovery stopped");
            }

            @Override
            public void onServiceFound(NsdServiceInfo serviceInfo) {
                Log.d(TAG, "Service discovery success" + serviceInfo);
                // Check if the service name matches your host_name
                if (serviceInfo.getServiceName().contains(TARGET_SERVICE_NAME)) {
                    // Resolve service to get IP and Port
                    mNsdManager.resolveService(serviceInfo, new ResolveListener() {
                        @Override
                        public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
                            Log.e(TAG, "Resolve failed: Error code " + errorCode);
                        }

                        @Override
                        public void onServiceResolved(NsdServiceInfo resolvedServiceInfo) {
                            Log.d(TAG, "Resolve Succeeded. " + resolvedServiceInfo);

                            // Get the IP and Port
                            String ip = resolvedServiceInfo.getHost().getHostAddress();
                            int port = resolvedServiceInfo.getPort();

                            if (mListener != null) {
                                mListener.onDeviceFound(ip, port);
                            }
                        }
                    });
                }
            }

            @Override
            public void onServiceLost(NsdServiceInfo serviceInfo) {
                Log.e(TAG, "service lost" + serviceInfo);
            }
        };
    }
}
