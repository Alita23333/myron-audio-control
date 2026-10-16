package dev.myron.audio;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.UserHandle;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/** Read-only local catalog, run by the root backend; no network or app launching. */
public final class AppCatalog {
    public static void main(String[] args) {
        try {
            android.os.Looper.prepareMainLooper();
            Class<?> activityThread = Class.forName("android.app.ActivityThread");
            Object thread = activityThread.getMethod("systemMain").invoke(null);
            Context context = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
            int user = (Integer) Class.forName("android.app.ActivityManager").getMethod("getCurrentUser").invoke(null);
            UserHandle handle = (UserHandle) UserHandle.class.getMethod("of", int.class).invoke(null, user);
            context = (Context) Context.class.getMethod("createContextAsUser", UserHandle.class, int.class).invoke(context, handle, 0);
            PackageManager pm = context.getPackageManager();
            List<ApplicationInfo> apps = new ArrayList<>(pm.getInstalledApplications(0));
            apps.sort(Comparator.comparing(app -> app.packageName));
            JSONArray result = new JSONArray();
            for (ApplicationInfo app : apps) {
                if (!app.packageName.matches("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+")) continue;
                CharSequence label;
                try { label = app.loadLabel(pm); } catch (RuntimeException e) { label = app.packageName; }
                result.put(new JSONObject().put("packageName", app.packageName)
                    .put("label", label == null ? app.packageName : label.toString())
                    .put("system", (app.flags & (ApplicationInfo.FLAG_SYSTEM | ApplicationInfo.FLAG_UPDATED_SYSTEM_APP)) != 0));
            }
            System.out.println(new JSONObject().put("ok", true).put("userId", user).put("apps", result));
            System.exit(0);
        } catch (Throwable error) {
            error.printStackTrace(System.err);
            System.exit(1);
        }
    }
}
