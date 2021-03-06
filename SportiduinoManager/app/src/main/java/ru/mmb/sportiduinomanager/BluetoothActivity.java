package ru.mmb.sportiduinomanager;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

import ru.mmb.sportiduinomanager.model.Chips;
import ru.mmb.sportiduinomanager.model.Distance;
import ru.mmb.sportiduinomanager.model.Station;
import ru.mmb.sportiduinomanager.task.ConnectDeviceTask;
import ru.mmb.sportiduinomanager.task.ResetStationTask;

/**
 * Provides ability to discover a station, connect to it and set it's mode.
 */
public final class BluetoothActivity extends MainActivity implements BTDeviceListAdapter.OnItemClicked {
    /**
     * Code of started Bluetooth discovery activity.
     */
    private static final int REQUEST_ENABLE_BT = 1;
    /**
     * Bluetooth adapter state: hardware is absent.
     */
    private static final int BT_STATE_ABSENT = 0;
    /**
     * Bluetooth adapter state: it is turned off.
     */
    private static final int BT_STATE_OFF = 1;
    /**
     * Bluetooth adapter state: it is turned on.
     */
    private static final int BT_STATE_ON = 2;
    /**
     * Bluetooth device search is not active.
     */
    private static final int BT_SEARCH_OFF = 0;
    /**
     * Bluetooth device search is active.
     */
    private static final int BT_SEARCH_ON = 1;
    /**
     * Station reset is not running now.
     */
    private static final int RESET_STATION_OFF = 0;
    /**
     * Station reset is in progress.
     */
    private static final int RESET_STATION_ON = 1;
    /**
     * RecyclerView with discovered Bluetooth devices and connect buttons.
     */
    private BTDeviceListAdapter mAdapter;

    /**
     * Reference to main thread object with all persistent data.
     */
    private MainApplication mMainApplication;
    /**
     * Bluetooth adapter handler.
     */
    private BluetoothAdapter mBluetoothAdapter;
    /**
     * Current state of bluetooth adapter.
     */
    private int mBluetoothState = BT_STATE_ABSENT;
    /**
     * Current state of bluetooth device discovery.
     */
    private int mBluetoothSearch = BT_SEARCH_OFF;
    /**
     * Current state of station reset procedure.
     */
    private int mResetStation = RESET_STATION_OFF;
    /**
     * Receiver of Bluetooth adapter state changes.
     */
    private final BroadcastReceiver mBTStateMonitor = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            // Get new Bluetooth state
            final int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.STATE_OFF);
            switch (state) {
                case BluetoothAdapter.STATE_ON:
                    if (mBluetoothState != BT_STATE_ON) {
                        // Bluetooth state was changed, update activity layout
                        mBluetoothState = BT_STATE_ON;
                        updateLayout(false);
                    }
                    break;
                case BluetoothAdapter.STATE_OFF:
                    if (mBluetoothState != BT_STATE_OFF) {
                        // Bluetooth state was changed, update activity layout
                        mBluetoothState = BT_STATE_OFF;
                        updateLayout(false);
                        // Ask to turn it on
                        final Intent enableBtIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
                        startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
                    }
                    break;
                default:
                    break;
            }
        }
    };
    /**
     * Receiver of Bluetooth device discovery events.
     */
    private final BroadcastReceiver mSearchDevices = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            final String action = intent.getAction();
            if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(action)) {
                mBluetoothSearch = BT_SEARCH_OFF;
                updateLayout(false);
            } else if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                // Discovery has found a device
                // Get the BluetoothDevice object and its info from the Intent
                final BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                mAdapter.insertItem(device);
                final List<BluetoothDevice> deviceList = mMainApplication.getBTDeviceList();
                if (!deviceList.contains(device)) {
                    deviceList.add(device);
                }
            }
        }
    };

    @Override
    protected void onCreate(final Bundle instanceState) {
        super.onCreate(instanceState);
        mMainApplication = (MainApplication) getApplication();
        setContentView(R.layout.activity_bluetooth);
        // Start monitoring bluetooth changes
        registerReceiver(mBTStateMonitor, new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED));
        // Prepare for Bluetooth device search
        final IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothDevice.ACTION_FOUND);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        registerReceiver(mSearchDevices, filter);
        // Prepare recycler view of device list
        final RecyclerView recyclerView = findViewById(R.id.device_list);
        // use a linear layout manager
        final RecyclerView.LayoutManager layoutManager = new LinearLayoutManager(this);
        recyclerView.setLayoutManager(layoutManager);
        // specify an RecyclerView adapter
        // and copy saved device list from main application
        mAdapter = new BTDeviceListAdapter(mMainApplication.getBTDeviceList(), this);
        final Station station = mMainApplication.getStation();
        if (station != null) mAdapter.setConnectedDevice(station.getAddress(), false);
        recyclerView.setAdapter(mAdapter);
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Set selection in drawer menu to current mode
        getMenuItem(R.id.bluetooth).setChecked(true);
        updateMenuItems(mMainApplication, R.id.bluetooth);
        // Disable startup animation
        overridePendingTransition(0, 0);
        // Initialize points and modes spinners
        final Spinner pointSpinner = findViewById(R.id.station_point_spinner);
        pointSpinner.setAdapter(getPointsAdapter());
        final PointSelectedListener onPointSelected = new PointSelectedListener();
        pointSpinner.setOnItemSelectedListener(onPointSelected);
        final Spinner modeSpinner = findViewById(R.id.station_mode_spinner);
        modeSpinner.setAdapter(getModesAdapter());
        // Check if device supports Bluetooth
        mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        if (mBluetoothAdapter == null) {
            // Device doesn't support Bluetooth
            mBluetoothState = BT_STATE_ABSENT;
            updateLayout(false);
            Toast.makeText(getApplicationContext(), getResources().getString(R.string.err_bt_absent),
                    Toast.LENGTH_LONG).show();
            return;
        }
        // Check if Bluetooth was turned on
        try {
            if (!mBluetoothAdapter.isEnabled()) {
                // Hide all elements and request Bluetooth
                mBluetoothState = BT_STATE_OFF;
                updateLayout(false);
                final Intent enableBtIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
                startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
                return;
            }
        } catch (SecurityException e) {
            // Bluetooth permission was withdrawn from the application
            mBluetoothState = BT_STATE_ABSENT;
            updateLayout(false);
            Toast.makeText(getApplicationContext(), getResources().getString(R.string.err_bt_forbidden),
                    Toast.LENGTH_LONG).show();
            return;
        }
        mBluetoothState = BT_STATE_ON;
        // Get Bluetooth search state
        if (mBluetoothAdapter.isDiscovering()) {
            mBluetoothSearch = BT_SEARCH_ON;
        } else {
            mBluetoothSearch = BT_SEARCH_OFF;
        }
        // Update activity layout
        updateLayout(true);
    }

    /**
     * Generator of list content for points dropdown list.
     *
     * @return Sorted list of all points of the distance
     */
    private ArrayAdapter<String> getPointsAdapter() {
        // Get list of active points (if a distance was downloaded)
        final Distance distance = mMainApplication.getDistance();
        List<String> points = new ArrayList<>();
        if (distance != null) {
            points = distance.getPointNames(getResources().getString(R.string.active_point_prefix));
        }
        return new ArrayAdapter<>(this, R.layout.support_simple_spinner_dropdown_item, points);
    }

    /**
     * Generator of list content for station modes dropdown list.
     *
     * @return List of all supported station modes
     */
    private ArrayAdapter<String> getModesAdapter() {
        final List<String> modes = Arrays.asList(
                getResources().getString(R.string.station_mode_0),
                getResources().getString(R.string.station_mode_1),
                getResources().getString(R.string.station_mode_2));
        return new ArrayAdapter<>(this, R.layout.support_simple_spinner_dropdown_item, modes);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Unregister Bluetooth state monitor
        unregisterReceiver(mBTStateMonitor);
        unregisterReceiver(mSearchDevices);
    }

    /**
     * Update private mBluetoothState on result of "Turn BT On" request.
     */
    @Override
    protected void onActivityResult(final int requestCode, final int resultCode, final Intent data) {
        if (requestCode != REQUEST_ENABLE_BT) return;
        if (resultCode == RESULT_OK) {
            mBluetoothState = BT_STATE_ON;
        } else {
            mBluetoothState = BT_STATE_OFF;
        }
        updateLayout(false);
    }

    /**
     * The onClick implementation of the RecyclerView item click.
     */
    @Override
    public void onItemClick(final int position) {
        final BluetoothDevice deviceClicked = mAdapter.getDevice(position);
        // Cancel Bluetooth discovery process
        if (mBluetoothAdapter.isDiscovering()) mBluetoothAdapter.cancelDiscovery();
        // Disconnect from previous station
        final Station station = mMainApplication.getStation();
        if (station != null) {
            station.disconnect();
            mMainApplication.setStation(null);
            updateMenuItems(mMainApplication, R.id.bluetooth);
            // If connected station is clicked, then just disconnect it
            if (station.getAddress().equals(deviceClicked.getAddress())) {
                mAdapter.setConnectedDevice(null, false);
                // Remove station info
                updateLayout(false);
                return;
            }
        }
        // Mark clicked device as being connected
        mAdapter.setConnectedDevice(deviceClicked.getAddress(), true);
        // Try to connect to device in background thread
        new ConnectDeviceTask(this, mAdapter).execute(deviceClicked);
    }

    /**
     * Search for available Bluetooth devices.
     *
     * @param view View of button clicked
     */
    public void searchForDevices(final View view) {
        // Don't try to search without Bluetooth
        if (mBluetoothAdapter == null) return;
        if (!mBluetoothAdapter.isEnabled()) {
            // Hide all elements and request Bluetooth
            mBluetoothState = BT_STATE_OFF;
            updateLayout(false);
            final Intent enableBtIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
            startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
            return;
        }
        // In Android >= 6.0 you have to ask for the runtime permission as well
        // in order for the discovery to get the devices ids. If you don't do this,
        // the discovery won't find any device.
        if (ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.ACCESS_COARSE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.ACCESS_COARSE_LOCATION}, 1);
        }
        // Empty device list in view and in the app
        mMainApplication.setBTDeviceList(new ArrayList<>());
        mAdapter.clearList();
        // Disconnect currently connected station
        final Station station = mMainApplication.getStation();
        if (station != null) {
            station.disconnect();
            mMainApplication.setStation(station);
        }
        // Start searching, API will return false if BT is turned off
        // (should not be the case but check it anyway)
        if (!mBluetoothAdapter.startDiscovery()) return;
        // Display progress indicator
        mBluetoothSearch = BT_SEARCH_ON;
        updateLayout(false);
    }

    /**
     * Set new mode and number for the station.
     *
     * @param view View of button clicked
     */
    public void changeStationMode(final View view) {
        // Get the station we work with
        final Station station = mMainApplication.getStation();
        if (station == null) return;
        // Get new station number
        final Spinner pointSpinner = findViewById(R.id.station_point_spinner);
        final Distance distance = mMainApplication.getDistance();
        int newNumber;
        if (distance == null) {
            newNumber = 0;
        } else {
            newNumber = distance.getNumberFromPosition(pointSpinner.getSelectedItemPosition());
        }
        // Get new station mode
        final Spinner modeSpinner = findViewById(R.id.station_mode_spinner);
        final int newMode = modeSpinner.getSelectedItemPosition();
        // Do nothing if numbers are the same
        final int currentNumber = station.getNumber();
        if (currentNumber == newNumber && newMode == station.getMode()) return;
        if (currentNumber != newNumber) {
            // Change reset station state
            mResetStation = RESET_STATION_ON;
            // Update activity layout
            updateResetProgress(0, 24);
            updateLayout(false);
            // Reset station to change it's number (and mode if needed)
            new ResetStationTask(this).execute(newNumber, newMode);
            return;
        }
        // If no station reset is needed,
        // then just call station mode change and display result
        station.newMode(newMode);
        onStationResetResult(station.getLastError(true));
    }

    /**
     * Called when station reset of station mode change is finished.
     *
     * @param result Result of previously called station reset / mode change
     */
    public void onStationResetResult(final int result) {
        // Turn RESET_STATION UI mode off
        mResetStation = RESET_STATION_OFF;
        // Save response time of last command (it'll be overwritten by getStatus)
        final Station station = mMainApplication.getStation();
        if (station == null) {
            updateLayout(false);
            updateMenuItems(mMainApplication, R.id.bluetooth);
            return;
        }
        long responseTime = station.getResponseTime();
        // Update layout
        if (result == 0) {
            // Make update with call to getStatus (just in case)
            updateLayout(true);
            updateMenuItems(mMainApplication, R.id.bluetooth);
            responseTime += station.getResponseTime();
        } else {
            updateLayout(false);
            updateMenuItems(mMainApplication, R.id.bluetooth);
            Toast.makeText(getApplicationContext(), result, Toast.LENGTH_LONG).show();
        }
        // Set correct response time as the sum of two last commands responses
        ((TextView) findViewById(R.id.station_response_time)).setText(getResources()
                .getString(R.string.response_time, responseTime));
    }

    /**
     * Synchronize station clock with Android clock.
     *
     * @param view View of button clicked
     */
    public void syncStationClock(final View view) {
        final Station station = mMainApplication.getStation();
        if (station == null) return;
        if (station.syncTime()) {
            ((TextView) findViewById(R.id.station_response_time)).setText(getResources()
                    .getString(R.string.response_time, station.getResponseTime()));
            ((TextView) findViewById(R.id.station_time_drift)).setText(getResources()
                    .getString(R.string.station_time_drift, station.getTimeDrift()));
        } else {
            Toast.makeText(getApplicationContext(), station.getLastError(true),
                    Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Update status of station reset process in activity layout.
     *
     * @param percents          Task percents already completed
     * @param secondsToComplete Estimated number of seconds to completion
     */
    public void updateResetProgress(final int percents, final int secondsToComplete) {
        ((ProgressBar) findViewById(R.id.station_reset_percents)).setProgress(percents);
        ((TextView) findViewById(R.id.station_reset_time)).setText(getResources()
                .getQuantityString(R.plurals.station_reset_time, secondsToComplete,
                        secondsToComplete));
    }

    /**
     * Update layout according to activity state.
     *
     * @param fetchStatus True if we need to send command to station
     *                    to get it's current status
     */
    public void updateLayout(final boolean fetchStatus) {
        // Hide station reset progress if station reset is not running
        if (mResetStation == RESET_STATION_OFF) {
            findViewById(R.id.station_reset_progress).setVisibility(View.GONE);
        }

        // Show BT search button / progress
        if (mBluetoothSearch == BT_SEARCH_OFF) {
            findViewById(R.id.device_search_progress).setVisibility(View.INVISIBLE);
            findViewById(R.id.device_search).setVisibility(View.VISIBLE);
        } else {
            findViewById(R.id.device_search).setVisibility(View.INVISIBLE);
            findViewById(R.id.device_search_progress).setVisibility(View.VISIBLE);
        }

        // If Bluetooth is absent/disabled - hide everything else
        if (mBluetoothState == BT_STATE_ABSENT || mBluetoothState == BT_STATE_OFF) {
            // Bluetooth is not working right now, disable everything
            findViewById(R.id.device_list).setVisibility(View.GONE);
            findViewById(R.id.station_status).setVisibility(View.GONE);
            return;
        }
        findViewById(R.id.device_list).setVisibility(View.VISIBLE);

        // If station reset is running - show it's progress and hide everything else
        if (mResetStation == RESET_STATION_ON) {
            findViewById(R.id.device_search).setVisibility(View.GONE);
            findViewById(R.id.device_search_progress).setVisibility(View.GONE);
            findViewById(R.id.device_list).setVisibility(View.GONE);
            findViewById(R.id.station_status).setVisibility(View.GONE);
            findViewById(R.id.station_reset_progress).setVisibility(View.VISIBLE);
            return;
        }

        // Don'try to update station status during BT search
        if (mBluetoothSearch == BT_SEARCH_ON) {
            findViewById(R.id.station_status).setVisibility(View.GONE);
            return;
        }

        // Show station status block
        showStationStatus(fetchStatus);
    }

    /**
     * Update station status block in layout.
     *
     * @param fetchStatus True if we need to send command to station
     *                    to get it's current status
     */
    private void showStationStatus(final boolean fetchStatus) {
        // Show station status block
        final Station station = mMainApplication.getStation();
        // Station was not connected yet
        if (station == null) {
            findViewById(R.id.station_status).setVisibility(View.GONE);
            return;
        }
        // Update station data if asked
        if (fetchStatus && (!station.fetchConfig() || !station.fetchStatus())) {
            Toast.makeText(getApplicationContext(), station.getLastError(true),
                    Toast.LENGTH_LONG).show();
            findViewById(R.id.station_status).setVisibility(View.GONE);
            return;
        }
        // Update station data status in layout
        ((TextView) findViewById(R.id.station_bt_name)).setText(station.getName());
        ((TextView) findViewById(R.id.station_firmware)).setText(getResources()
                .getString(R.string.station_firmware, station.getFirmware()));
        ((TextView) findViewById(R.id.station_voltage)).setText(getResources()
                .getString(R.string.station_voltage,
                        station.getVoltage(), station.getTemperature()));
        ((TextView) findViewById(R.id.station_response_time)).setText(getResources()
                .getString(R.string.response_time, station.getResponseTime()));
        final Distance distance = mMainApplication.getDistance();
        String pointName;
        if (distance == null) {
            pointName = "#" + station.getNumber();
        } else {
            pointName = distance.getPointName(station.getNumber(),
                    getResources().getString(R.string.active_point_prefix));
        }
        switch (station.getMode()) {
            case Station.MODE_INIT_CHIPS:
                ((TextView) findViewById(R.id.station_mode_value)).setText(getResources()
                        .getString(R.string.station_mode_value_0, pointName));
                break;
            case Station.MODE_OTHER_POINT:
                ((TextView) findViewById(R.id.station_mode_value)).setText(getResources()
                        .getString(R.string.station_mode_value_1, pointName));
                break;
            case Station.MODE_FINISH_POINT:
                ((TextView) findViewById(R.id.station_mode_value)).setText(getResources()
                        .getString(R.string.station_mode_value_2, pointName));
                break;
            default:
                ((TextView) findViewById(R.id.station_mode_value))
                        .setText(R.string.station_mode_unknown);
                break;
        }

        // Update drop down lists according to current station number and default mode
        final Spinner pointSpinner = findViewById(R.id.station_point_spinner);
        int position;
        if (distance == null) {
            position = 0;
        } else {
            position = distance.getPositionFromNumber(station.getNumber());
        }
        pointSpinner.setSelection(position);
        final Spinner modeSpinner = findViewById(R.id.station_mode_spinner);
        modeSpinner.setSelection(station.getMode());

        ((TextView) findViewById(R.id.station_time_drift)).setText(getResources()
                .getString(R.string.station_time_drift, station.getTimeDrift()));
        ((TextView) findViewById(R.id.station_chips_registered_value))
                .setText(String.format(Locale.getDefault(), "%d", station.getChipsRegistered()));
        ((TextView) findViewById(R.id.station_last_chip_time)).setText(
                Chips.printTime(station.getLastChipTime(), "dd.MM.yyyy  HH:mm:ss"));
        // Show status block
        findViewById(R.id.station_status).setVisibility(View.VISIBLE);

    }

    /**
     * Callback to be invoked when an item in points list has been selected.
     */
    class PointSelectedListener implements AdapterView.OnItemSelectedListener {

        @Override
        public void onItemSelected(final AdapterView<?> parent, final View view, final int position,
                                   final long rowId) {
            // Get point number at this position
            final Distance distance = mMainApplication.getDistance();
            int pointNumber;
            if (distance == null) {
                pointNumber = 0;
            } else {
                pointNumber = distance.getNumberFromPosition(parent.getSelectedItemPosition());
            }
            // Compute mode for this point
            // Use station mode for current point and default mode for all other points
            int pointMode;
            final Station station = mMainApplication.getStation();
            if (station != null && pointNumber == station.getNumber()) {
                pointMode = station.getMode();
            } else {
                pointMode = getPointDefaultMode(pointNumber);
            }
            // Select new mode in modes list
            if (pointMode >= 0) {
                ((Spinner) findViewById(R.id.station_mode_spinner)).setSelection(pointMode);
            }
        }

        /**
         * Get default mode for selected point.
         *
         * @param pointNumber Point number in downloaded distance
         * @return One of Station.MODE_* constants or -1 in case of error
         */
        private int getPointDefaultMode(final int pointNumber) {
            final Distance distance = mMainApplication.getDistance();
            if (distance == null) return -1;
            switch (distance.getPointType(pointNumber)) {
                case -1:
                    return -1;
                case 0:
                    return Station.MODE_INIT_CHIPS;
                case 2:
                    return Station.MODE_FINISH_POINT;
                default:
                    return Station.MODE_OTHER_POINT;
            }
        }

        @Override
        public void onNothingSelected(final AdapterView<?> parent) {
            // do nothing
        }
    }
}
