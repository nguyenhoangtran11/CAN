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

import android.content.Intent
import android.os.Bundle
import android.text.TextUtils
import android.util.Log
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.content.res.AppCompatResources
import com.nht.AppConstants
import com.nht.provisioning.CanConstants
import com.nht.provisioning.DeviceConnectionEvent
import com.nht.provisioning.CanProvisionManager
import com.nht.wifi_provisioning.R
import com.nht.wifi_provisioning.databinding.ActivityWifiConfigBinding
import org.greenrobot.eventbus.EventBus
import org.greenrobot.eventbus.Subscribe
import org.greenrobot.eventbus.ThreadMode

class WiFiConfigActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWifiConfigBinding

    private lateinit var provisionManager: CanProvisionManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWifiConfigBinding.inflate(layoutInflater)
        setContentView(binding.root)

        provisionManager = CanProvisionManager.getInstance(applicationContext)
        initViews()
        EventBus.getDefault().register(this)
    }

    override fun onDestroy() {
        EventBus.getDefault().unregister(this)
        super.onDestroy()
    }

    override fun onBackPressed() {
        provisionManager.espDevice.disconnectDevice()
        super.onBackPressed()
    }

    @Subscribe(threadMode = ThreadMode.MAIN)
    fun onEvent(event: DeviceConnectionEvent) {
        Log.d(TAG, "On Device Connection Event RECEIVED : " + event.eventType)

        when (event.eventType) {
            CanConstants.EVENT_DEVICE_DISCONNECTED -> if (!isFinishing) {
                showAlertForDeviceDisconnected()
            }
        }
    }

    private val nextBtnClickListener = View.OnClickListener {
        val ssid = binding.layoutWifiConfig.etSsidInput.text.toString()
        val password = binding.layoutWifiConfig.etPasswordInput.text.toString()

        if (TextUtils.isEmpty(ssid)) {
            binding.layoutWifiConfig.etSsidInput.error = getString(R.string.error_ssid_empty)
            return@OnClickListener
        }
        goToProvisionActivity(ssid, password)
    }

    private fun initViews() {
        setToolbar()
        val deviceName = provisionManager.espDevice.deviceName
        if (!TextUtils.isEmpty(deviceName)) {
            val msg = String.format(getString(R.string.setup_instructions), deviceName)
            val tvInstructionMsg = findViewById<TextView>(R.id.setup_instructions_view)
            tvInstructionMsg.text = msg
        }

        binding.layoutWifiConfig.btnNext.textBtn.setText(R.string.btn_next)
        binding.layoutWifiConfig.btnNext.layoutBtn.setOnClickListener(nextBtnClickListener)
    }

    private fun setToolbar() {
        setSupportActionBar(binding.titleBar.toolbar)
        supportActionBar!!.setDisplayHomeAsUpEnabled(true)
        supportActionBar!!.setDisplayShowHomeEnabled(true)
        supportActionBar!!.setTitle(R.string.title_activity_wifi_config)
        binding.titleBar.toolbar.navigationIcon =
            AppCompatResources.getDrawable(this, R.drawable.ic_arrow_left)
        binding.titleBar.toolbar.setNavigationOnClickListener {
            provisionManager.espDevice?.disconnectDevice()
            finish()
        }
    }

    private fun goToProvisionActivity(ssid: String, password: String) {
        finish()
        val provisionIntent = Intent(applicationContext, ProvisionActivity::class.java)
        provisionIntent.putExtras(intent)
        provisionIntent.putExtra(AppConstants.KEY_WIFI_SSID, ssid)
        provisionIntent.putExtra(AppConstants.KEY_WIFI_PASSWORD, password)
        startActivity(provisionIntent)
    }

    private fun showAlertForDeviceDisconnected() {
        val builder = AlertDialog.Builder(this)
        builder.setCancelable(false)
        builder.setTitle(R.string.error_title)
        builder.setMessage(R.string.dialog_msg_ble_device_disconnection)

        // Set up the buttons
        builder.setPositiveButton(
            R.string.btn_ok
        ) { dialog, which ->
            dialog.dismiss()
            finish()
        }
        builder.show()
    }

    companion object {
        private val TAG: String = WiFiConfigActivity::class.java.simpleName
    }
}
