package ru.mmb.sportiduinomanager;

import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.support.v4.content.res.ResourcesCompat;
import android.support.v7.widget.SwitchCompat;
import android.view.MenuItem;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.lang.ref.WeakReference;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Scanner;

import ru.mmb.sportiduinomanager.model.Distance;

/**
 * Provides interaction with database at http://mmb.progressor.ru
 */
public class DatabaseActivity extends MainActivity {
    /**
     * URL of test database interaction script.
     */
    private static final String TEST_DATABASE_URL = "http://mmb.progressor.ru/php/mmbscripts_git/sportiduino.php";
    /**
     * URL of main database interaction script.
     */
    private static final String MAIN_DATABASE_URL = "http://mmb.progressor.ru/php/mmbscripts/sportiduino.php";
    /**
     * Website script API version supported by this application.
     */
    private static final String HTTP_API_VERSION = "1";

    /**
     * Local copy of distance (downloaded from site or loaded from local database).
     */
    private Distance mDistance;

    /**
     * Main application thread with persistent data.
     */
    private MainApplication mMainApplication;
    /**
     * Async download thread manager.
     */
    private DownloadManager mDownloadManager;
    /**
     * Copy of activity context for AsyncTask.
     */
    private DatabaseActivity mContext;

    /**
     * Receiver of "download completed" events.
     */
    private final BroadcastReceiver mDistanceReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            // check if it was our download which have been completed
            final long downloadId = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, 0L);
            if (downloadId != mMainApplication.getDistanceDownloadId()) {
                return;
            }
            // check download status
            final Cursor cursor = mDownloadManager.query(
                    new DownloadManager.Query().setFilterById(downloadId));
            if (cursor == null || !cursor.moveToFirst()) {
                return;
            }
            if (cursor.getInt(cursor.getColumnIndex(DownloadManager.COLUMN_STATUS))
                    != DownloadManager.STATUS_SUCCESSFUL) {
                Toast.makeText(context, getResources().getString(R.string.err_db_download_failed)
                                + cursor.getInt(cursor.getColumnIndex(DownloadManager.COLUMN_REASON)),
                        Toast.LENGTH_LONG).show();
                cursor.close();
                return;
            }
            final String path = Uri.parse(cursor
                    .getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_URI))).getPath();
            cursor.close();
            // Parse the file and load it to database in background thread
            new LoadDistance(mContext).execute(path);
        }
    };

    // Calculate MD5 from user password string
    private String md5(final String str) {
        try {
            // Create MD5 Hash
            final MessageDigest digest = MessageDigest.getInstance("MD5");
            digest.update(str.getBytes(Charset.forName("UTF-8")));
            final byte[] messageDigest = digest.digest();

            // Create Hex String
            final StringBuilder hexString = new StringBuilder();
            for (final byte aMessageDigest : messageDigest) {
                final String hexNumber = Integer.toHexString(0xFF & aMessageDigest);
                if (hexNumber.length() < 2) {
                    hexString.append('0');
                }
                hexString.append(hexNumber);
            }
            return hexString.toString();

        } catch (NoSuchAlgorithmException e) {
            return "";
        }
    }

    @Override
    protected void onCreate(final Bundle instanceState) {
        super.onCreate(instanceState);
        mContext = this;
        mMainApplication = (MainApplication) this.getApplication();
        mDistance = mMainApplication.getDistance();
        mDownloadManager = (DownloadManager) getSystemService(DOWNLOAD_SERVICE);
        setContentView(R.layout.activity_database);
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Set selection in drawer menu to current mode
        getMenuItem(R.id.database).setChecked(true);
        // Disable startup animation
        overridePendingTransition(0, 0);
        // Update layout elements
        updateLayout();
        // Register download receiver
        registerReceiver(mDistanceReceiver, new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE));
    }

    @Override
    protected void onStop() {
        super.onStop();
        // Unregister download receiver
        unregisterReceiver(mDistanceReceiver);
    }

    private void updateLayout() {
        // Get database status from persistent memory
        final int dbStatus = mMainApplication.getDbStatus();
        // Hide progress bar and update database status string
        final TextView statusMessage = findViewById(R.id.database_status_description);
        int statusColor;
        if (dbStatus == Distance.DB_STATE_EMPTY || dbStatus == Distance.DB_STATE_OK) {
            statusColor = R.color.text_primary;
        } else {
            statusColor = R.color.bg_secondary;
        }
        statusMessage.setTextColor(ResourcesCompat.getColor(getResources(), statusColor, getTheme()));
        statusMessage.setText(Distance.getStatusMessage(dbStatus));
        if (mMainApplication.getDistanceDownloadId() == -1) {
            findViewById(R.id.database_status_progress).setVisibility(View.INVISIBLE);
            statusMessage.setVisibility(View.VISIBLE);
        } else {
            statusMessage.setVisibility(View.INVISIBLE);
            findViewById(R.id.database_status_progress).setVisibility(View.VISIBLE);
        }
        // Detect what we will show or hide
        final MenuItem databaseItem = getMenuItem(R.id.database);
        final Button getResultsButton = findViewById(R.id.get_results);
        final Button sendResultsButton = findViewById(R.id.send_results);
        final LinearLayout dlDistanceLayout = findViewById(R.id.download_distance_layout);
        final LinearLayout dbContentLayout = findViewById(R.id.database_content_layout);
        switch (dbStatus) {
            case Distance.DB_STATE_FAILED:
                // Database is broken, can't do anything
                databaseItem.setTitle(getResources().getText(R.string.mode_cloud_download));
                databaseItem.setIcon(R.drawable.ic_cloud_download);
                getResultsButton.setVisibility(View.GONE);
                sendResultsButton.setVisibility(View.GONE);
                dlDistanceLayout.setVisibility(View.GONE);
                dbContentLayout.setVisibility(View.GONE);
                break;
            case Distance.DB_STATE_EMPTY:
                // Database is empty, need to download it from server
                databaseItem.setTitle(getResources().getText(R.string.mode_cloud_download));
                databaseItem.setIcon(R.drawable.ic_cloud_download);
                getResultsButton.setVisibility(View.GONE);
                sendResultsButton.setVisibility(View.GONE);
                dlDistanceLayout.setVisibility(View.VISIBLE);
                dbContentLayout.setVisibility(View.GONE);
                break;
            case Distance.DB_STATE_OUTDATED:
            case Distance.DB_STATE_OK:
                mDistance = mMainApplication.getDistance();
                // Don't allow to reload database if it contains important data
                if (mDistance.canBeReloaded()) {
                    dlDistanceLayout.setVisibility(View.VISIBLE);
                } else {
                    dlDistanceLayout.setVisibility(View.GONE);
                }
                // set user email and test db flag from local database
                ((EditText) findViewById(R.id.user_email)).setText(mMainApplication.getUserEmail());
                ((SwitchCompat) findViewById(R.id.test_database)).setChecked(mMainApplication.getTestSite() == 1);
                // TODO: add check for showing UL/DL buttons
                getResultsButton.setAlpha(.5f);
                getResultsButton.setClickable(false);
                getResultsButton.setVisibility(View.VISIBLE);
                sendResultsButton.setAlpha(.5f);
                sendResultsButton.setClickable(false);
                sendResultsButton.setVisibility(View.VISIBLE);
                // Show database content
                String siteName;
                if (mDistance.getTestSite() == 0) {
                    siteName = (String) getResources().getText(R.string.site_name_main);
                } else {
                    siteName = (String) getResources().getText(R.string.site_name_test);
                }
                ((TextView) findViewById(R.id.distance_version)).setText(getResources()
                        .getString(R.string.database_distance_version, siteName,
                                mDistance.getDownloadDate()));
                ((TextView) findViewById(R.id.distance_name)).setText(mDistance.getRaidName());
                dbContentLayout.setVisibility(View.VISIBLE);
                // Update main menu item
                databaseItem.setTitle(getResources().getText(R.string.mode_cloud_done));
                databaseItem.setIcon(R.drawable.ic_cloud_done);
                break;
            default:
        }
    }

    /**
     * Start download of last distance from site.
     *
     * @param view View of button clicked
     */
    public void startDistanceDownload(final View view) {
        // Check if we have another download waiting
        if (mMainApplication.getDistanceDownloadId() != -1) {
            Toast.makeText(getApplicationContext(), getResources().getString(R.string.err_db_download_waiting),
                    Toast.LENGTH_LONG).show();
            return;
        }

        // check for empty/bad values
        final EditText etUserEmail = findViewById(R.id.user_email);
        final String sUserEmail = etUserEmail.getText().toString();
        if (sUserEmail.isEmpty()) {
            etUserEmail.setError(getResources().getString(R.string.err_db_empty_email));
            return;
        }
        if (!sUserEmail.contains("@")) {
            etUserEmail.setError(getResources().getString(R.string.err_db_bad_email));
            return;
        }
        final EditText etUserPassword = findViewById(R.id.user_password);
        String sUserPassword = etUserPassword.getText().toString();
        if (sUserPassword.isEmpty()) {
            etUserPassword.setError(getResources().getString(R.string.err_db_empty_password));
            return;
        }
        sUserPassword = md5(sUserPassword);

        // get download url
        final int testSite = ((SwitchCompat) findViewById(R.id.test_database)).isChecked() ? 1 : 0;
        String url;
        if (testSite == 0) {
            url = MAIN_DATABASE_URL;
        } else {
            url = TEST_DATABASE_URL;
        }

        // Save email/password/site in main application
        // (as this activity can be recreated loosing these value)
        mMainApplication.setAuthorizationParameters(sUserEmail, sUserPassword, testSite);

        // Clean password field to require to enter it again for next distance download
        etUserPassword.setText("");
        // Hide virtual keyboard
        final InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
        }
        // Show progress bar instead of status text
        findViewById(R.id.database_status_description).setVisibility(View.INVISIBLE);
        findViewById(R.id.database_status_progress).setVisibility(View.VISIBLE);

        // start download
        final DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
        request.addRequestHeader("X-Sportiduino-Protocol", HTTP_API_VERSION);
        request.addRequestHeader("X-Sportiduino-Auth", sUserEmail + "|" + sUserPassword);
        request.addRequestHeader("X-Sportiduino-Action", "1");
        request.setTitle(getResources().getString(R.string.app_name));
        request.setDestinationInExternalFilesDir(getApplicationContext(), null, "distance.temp");
        mMainApplication.setDistanceDownloadId(mDownloadManager.enqueue(request));
    }

    /**
     * Separate thread for async parsing of downloaded file with a distance.
     */
    private static class LoadDistance extends AsyncTask<String, Void, Integer> {
        /**
         * Reference to parent activity (which can cease to exist in any moment).
         */
        private final WeakReference<DatabaseActivity> activityReference;
        /**
         * Reference to main application thread.
         */
        private final MainApplication mainApplication;
        /**
         * Downloaded file with a distance.
         */
        private File mFile;
        /**
         * Custom string which cannot be loaded from resources.
         */
        private String mCustomError;

        // Retain only a weak reference to the activity
        LoadDistance(final DatabaseActivity context) {
            super();
            activityReference = new WeakReference<>(context);
            mainApplication = (MainApplication) context.getApplication();
        }

        /**
         * Process server response and save distance and teams to SQLite database.
         *
         * @param path Path to file
         * @return True if succeeded
         */
        protected Integer doInBackground(final String... path) {
            // Create scanner for file parsing
            mFile = new File(path[0]);
            Scanner scanner;
            try {
                scanner = new Scanner(mFile, "UTF-8").useDelimiter("[\t\n]");
            } catch (FileNotFoundException e) {
                return R.string.err_db_reading_response;
            }
            // check for error message from server
            if (!scanner.hasNextLine()) return R.string.err_db_reading_response;
            final String message = scanner.nextLine();
            if (!"".equals(message)) {
                mCustomError = message;
                return -1;
            }
            // read the response
            Distance distance = null;
            boolean oldDistance = true;
            while (scanner.hasNextLine()) {
                final String blockType = scanner.next();
                switch (blockType) {
                    case "R":
                        // get raid information
                        final int raidId = scanner.nextInt();
                        final long raidTimeReadonly = scanner.nextLong();
                        final long raidTimeFinish = scanner.nextLong();
                        final String raidName = scanner.next();
                        distance = new Distance(raidId, raidTimeReadonly, raidTimeFinish, raidName,
                                mainApplication.getUserEmail(), mainApplication.getUserPassword(),
                                mainApplication.getTestSite());
                        oldDistance = false;
                        break;
                    case "P":
                        // parse list of points
                        if (oldDistance) return R.string.err_db_bad_response;
                        final int nPoints = scanner.nextInt();
                        final int maxOrder = scanner.nextInt();
                        distance.initPointArray(maxOrder,
                                mainApplication.getContext().getResources().getString(R.string.mode_chip_init));
                        for (int i = 0; i < nPoints; i++) {
                            if (!"".equals(scanner.next())) return R.string.err_db_bad_response;
                            final int index = scanner.nextInt();
                            final int type = scanner.nextInt();
                            final int penalty = scanner.nextInt();
                            final long start = scanner.nextLong();
                            final long end = scanner.nextLong();
                            final String name = scanner.next();
                            if (!distance.addPoint(index, type, penalty, start, end, name)) {
                                return R.string.err_db_bad_response;
                            }
                        }
                        break;
                    case "D":
                        // parse list of discounts
                        if (oldDistance) return R.string.err_db_bad_response;
                        final int nDiscounts = scanner.nextInt();
                        distance.initDiscountArray(nDiscounts);
                        for (int i = 0; i < nDiscounts; i++) {
                            if (!"".equals(scanner.next())) return R.string.err_db_bad_response;
                            final int minutes = scanner.nextInt();
                            final int fromPoint = scanner.nextInt();
                            final int toPoint = scanner.nextInt();
                            if (!distance.addDiscount(minutes, fromPoint, toPoint)) {
                                return R.string.err_db_bad_response;
                            }
                        }
                        break;
                    case "T":
                        // parse list of teams
                        if (oldDistance) return R.string.err_db_bad_response;
                        final int nTeams = scanner.nextInt();
                        final int maxNumber = scanner.nextInt();
                        distance.initTeamArray(maxNumber);
                        for (int i = 0; i < nTeams; i++) {
                            if (!"".equals(scanner.next())) return R.string.err_db_bad_response;
                            final int number = scanner.nextInt();
                            final int nMembers = scanner.nextInt();
                            final int nMaps = scanner.nextInt();
                            final String name = scanner.next();
                            if (!distance.addTeam(number, nMembers, nMaps, name)) {
                                return R.string.err_db_bad_response;
                            }
                        }
                        break;
                    case "M":
                        // parse list of team members
                        if (oldDistance) return R.string.err_db_bad_response;
                        final int nMembers = scanner.nextInt();
                        for (int i = 0; i < nMembers; i++) {
                            if (!"".equals(scanner.next())) return R.string.err_db_bad_response;
                            final long memberId = scanner.nextLong();
                            final int team = scanner.nextInt();
                            final String name = scanner.next();
                            final String phone = scanner.next();
                            if (!distance.addMember(memberId, team, name, phone)) {
                                return R.string.err_db_bad_response;
                            }
                        }
                        break;
                    case "E":
                        // End of distance data in server response
                        break;
                    default:
                        return R.string.err_db_bad_response;
                }
                if ("E".equals(blockType)) break;
            }
            scanner.close();
            // check if all necessary data were present
            if (oldDistance) return R.string.err_db_bad_response;
            // Validate loaded distance
            if (distance.hasErrors()) {
                // Downloaded distance had errors, restore old distance from persistent memory
                return R.string.err_db_bad_response;
            }
            // Copy parsed distance to persistent memory
            mainApplication.setDistance(distance);
            // Save parsed distance to local database
            final String result = distance.saveToDb(mainApplication.getDatabase());
            mainApplication.updateDbStatus();
            if (result != null) {
                mCustomError =
                        mainApplication.getContext().getResources().getString(R.string.err_db_saving) + ": " + result;
                return -1;
            }
            // TODO: reload distance from database to ensure that it was saved correctly
            return R.string.download_distance_success;
        }

        /**
         * Show parsing result, delete the file and update screen layout.
         *
         * @param message False if connection attempt failed
         */
        protected void onPostExecute(final Integer message) {
            // Show parsing result
            if (mCustomError == null) {
                Toast.makeText(mainApplication, message, Toast.LENGTH_LONG).show();
            } else {
                Toast.makeText(mainApplication, mCustomError, Toast.LENGTH_LONG).show();
            }
            // Delete downloaded file
            if (!mFile.delete()) {
                Toast.makeText(mainApplication, R.string.err_db_reading_response,
                        Toast.LENGTH_LONG).show();
            }
            mainApplication.setDistanceDownloadId(-1L);
            // Get a reference to the activity if it is still there
            final DatabaseActivity activity = activityReference.get();
            if (activity == null || activity.isFinishing()) return;
            // Update activity layout
            activity.updateLayout();
        }
    }

}