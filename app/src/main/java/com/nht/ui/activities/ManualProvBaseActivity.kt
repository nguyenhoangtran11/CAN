// Copyright 2026 by NHT
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.nht.ui.activities

import android.content.SharedPreferences
import android.os.Bundle
import android.text.TextUtils
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import com.nht.AppConstants
import com.nht.provisioning.CanConstants
import com.nht.provisioning.CanProvisionManager
import com.nht.wifi_provisioning.R
import org.greenrobot.eventbus.EventBus
import org.json.JSONException
import org.json.JSONObject

open class ManualProvBaseActivity : AppCompatActivity() {

    companion object {
        private val TAG: String = ManualProvBaseActivity::class.java.simpleName
    }

    @JvmField
    var securityType: Int = 0

    @JvmField
    var provisionManager: CanProvisionManager? = null

    @JvmField
    var sharedPreferences: SharedPreferences? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_provision_landing)
        securityType =
            intent.getIntExtra(AppConstants.KEY_SECURITY_TYPE, AppConstants.SEC_TYPE_DEFAULT)
        provisionManager = CanProvisionManager.getInstance(applicationContext)
        sharedPreferences = getSharedPreferences(AppConstants.ESP_PREFERENCES, MODE_PRIVATE)
        EventBus.getDefault().register(this)
    }

    override fun onDestroy() {
        EventBus.getDefault().unregister(this)
        super.onDestroy()
    }

    override fun onBackPressed() {
        if (provisionManager!!.espDevice != null) {
            provisionManager!!.espDevice.disconnectDevice()
        }
        super.onBackPressed()
    }

    fun setSecurityTypeFromVersionInfo() {
        val protoVerStr = provisionManager!!.espDevice.versionInfo

        try {
            val jsonObject = JSONObject(protoVerStr)
            val provInfo = jsonObject.getJSONObject("prov")

            if (provInfo != null) {
                if (provInfo.has("sec_ver")) {
                    val serVer = provInfo.optInt("sec_ver")
                    Log.d(TAG, "Security Version : $serVer")

                    when (serVer) {
                        AppConstants.SEC_TYPE_0 -> {
                            securityType = AppConstants.SEC_TYPE_0
                            provisionManager!!.espDevice.securityType =
                                CanConstants.SecurityType.SECURITY_0
                        }

                        AppConstants.SEC_TYPE_1 -> {
                            securityType = AppConstants.SEC_TYPE_1
                            provisionManager!!.espDevice.securityType =
                                CanConstants.SecurityType.SECURITY_1
                        }

                        AppConstants.SEC_TYPE_2 -> {
                            securityType = AppConstants.SEC_TYPE_2
                            provisionManager!!.espDevice.securityType =
                                CanConstants.SecurityType.SECURITY_2
                            val deviceCaps = provisionManager!!.espDevice.deviceCapabilities

                            if (deviceCaps != null && deviceCaps.size > 0 && (deviceCaps.contains(
                                    AppConstants.CAPABILITY_THREAD_SCAN
                                ) || deviceCaps.contains(AppConstants.CAPABILITY_THREAD_PROV))
                            ) {
                                val userName = sharedPreferences!!.getString(
                                    AppConstants.KEY_USER_NAME_THREAD,
                                    AppConstants.DEFAULT_USER_NAME_THREAD
                                )!!
                                provisionManager!!.espDevice.userName = userName
                            } else if (TextUtils.isEmpty(provisionManager!!.espDevice.userName)) {
                                val userName = sharedPreferences!!.getString(
                                    AppConstants.KEY_USER_NAME_WIFI,
                                    AppConstants.DEFAULT_USER_NAME_WIFI
                                )!!
                                provisionManager!!.espDevice.userName = userName
                            }
                        }

                        else -> {
                            securityType = AppConstants.SEC_TYPE_2
                            provisionManager!!.espDevice.securityType =
                                CanConstants.SecurityType.SECURITY_2
                            val deviceCaps = provisionManager!!.espDevice.deviceCapabilities

                            if (deviceCaps != null && deviceCaps.size > 0 && (deviceCaps.contains(
                                    AppConstants.CAPABILITY_THREAD_SCAN
                                ) || deviceCaps.contains(AppConstants.CAPABILITY_THREAD_PROV))
                            ) {
                                val userName = sharedPreferences!!.getString(
                                    AppConstants.KEY_USER_NAME_THREAD,
                                    AppConstants.DEFAULT_USER_NAME_THREAD
                                )!!
                                provisionManager!!.espDevice.userName = userName
                            } else if (TextUtils.isEmpty(provisionManager!!.espDevice.userName)) {
                                val userName = sharedPreferences!!.getString(
                                    AppConstants.KEY_USER_NAME_WIFI,
                                    AppConstants.DEFAULT_USER_NAME_WIFI
                                )!!
                                provisionManager!!.espDevice.userName = userName
                            }
                        }
                    }
                } else {
                    if (securityType == AppConstants.SEC_TYPE_2) {
                        securityType = AppConstants.SEC_TYPE_1
                        provisionManager!!.espDevice.securityType =
                            CanConstants.SecurityType.SECURITY_1
                    }
                }
            } else {
                Log.e(TAG, "proto-ver info is not available.")
            }
        } catch (e: JSONException) {
            e.printStackTrace()
            Log.d(TAG, "Capabilities JSON not available.")
        }
    }
}
