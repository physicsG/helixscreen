// SPDX-License-Identifier: GPL-3.0-or-later
package org.helixscreen.app;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.util.Log;

import java.util.ArrayList;

/**
 * Tells native code which Wi-Fi network carries the app's LAN sockets.
 *
 * Android makes a Wi-Fi network with no internet access the default only when nothing else
 * is available. With mobile data up, a socket to a 192.168.x.x printer goes out over the
 * cellular link and times out. Native code (android_lan_route.cpp) binds each socket whose
 * peer is a LAN address to the network handle published here; the process itself stays
 * unbound, so internet traffic keeps the system's default route.
 *
 * The published handle is 0 (no LAN binding) when:
 *   - no Wi-Fi is connected, so a printer reached over mobile data still works;
 *   - a VPN is the default network, so a printer reached through Tailscale or another VPN,
 *     including a subnet route to its LAN, still goes through it.
 */
final class WifiBinder {
    private static final String TAG = "HelixNetwork";

    private final ConnectivityManager mConnectivity;
    private final Object mLock = new Object();
    /** Connected Wi-Fi networks, oldest first. More than one with dual-STA. */
    private final ArrayList<Network> mWifi = new ArrayList<>();
    private boolean mDefaultIsVpn;
    private boolean mRunning;
    private long mPublished;
    private ConnectivityManager.NetworkCallback mWifiCallback;
    private ConnectivityManager.NetworkCallback mDefaultCallback;

    WifiBinder(Context context) {
        mConnectivity = context.getSystemService(ConnectivityManager.class);
    }

    void start() {
        if (mConnectivity == null || mWifiCallback != null) {
            return;
        }
        synchronized (mLock) {
            mRunning = true;
        }
        // A printer's own access point may not claim internet, so do not require it.
        NetworkRequest wifiRequest = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        mWifiCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                synchronized (mLock) {
                    mWifi.remove(network);
                    mWifi.add(network);
                    publishLocked("Wi-Fi available");
                }
            }

            @Override
            public void onLost(Network network) {
                synchronized (mLock) {
                    mWifi.remove(network);
                    publishLocked("Wi-Fi lost");
                }
            }
        };
        mDefaultCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                synchronized (mLock) {
                    mDefaultIsVpn = caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN);
                    publishLocked("default network changed");
                }
            }

            @Override
            public void onLost(Network network) {
                synchronized (mLock) {
                    mDefaultIsVpn = false;
                    publishLocked("default network lost");
                }
            }
        };
        try {
            mConnectivity.registerNetworkCallback(wifiRequest, mWifiCallback);
            mConnectivity.registerDefaultNetworkCallback(mDefaultCallback);
        } catch (RuntimeException e) {
            Log.w(TAG, "Network callback registration failed: " + e);
            stop();
        }
    }

    void stop() {
        if (mConnectivity == null) {
            return;
        }
        // Stop first, so a callback already running on the connectivity thread cannot
        // publish a handle after this returns.
        synchronized (mLock) {
            mRunning = false;
            mWifi.clear();
            mDefaultIsVpn = false;
            publishHandleLocked(0, "stopped");
        }
        for (ConnectivityManager.NetworkCallback cb :
                new ConnectivityManager.NetworkCallback[] {mWifiCallback, mDefaultCallback}) {
            if (cb == null) {
                continue;
            }
            try {
                mConnectivity.unregisterNetworkCallback(cb);
            } catch (RuntimeException e) {
                // Never registered: nothing to undo.
            }
        }
        mWifiCallback = null;
        mDefaultCallback = null;
    }

    private void publishLocked(String why) {
        if (!mRunning) {
            return;
        }
        // The most recently connected Wi-Fi is the one the user just joined.
        Network wifi = mWifi.isEmpty() ? null : mWifi.get(mWifi.size() - 1);
        long handle = (wifi != null && !mDefaultIsVpn) ? wifi.getNetworkHandle() : 0;
        publishHandleLocked(handle, why + (mDefaultIsVpn ? ", VPN active" : ""));
    }

    private void publishHandleLocked(long handle, String why) {
        if (handle == mPublished) {
            return;
        }
        try {
            nativeSetWifiNetwork(handle);
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Native library unavailable, LAN sockets keep the default route: " + e);
            return;
        }
        mPublished = handle;
        if (handle != 0) {
            Log.i(TAG, "LAN sockets use Wi-Fi network " + handle + " (" + why + ")");
        } else {
            Log.i(TAG, "LAN sockets use the default network (" + why + ")");
        }
    }

    private static native void nativeSetWifiNetwork(long handle);
}
