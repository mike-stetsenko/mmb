<?php

// Общие настройки
include("settings.php");
// Библиотека функций
include("functions.php");

// Первичная проверка данных авторизации
if ($_GET['Login'] == "") 
{
   print("Login is missing");
   return;

} elseif ($_GET['Password']== "") {
   print("Password is missing");
   return;
} 

// Аутентификация и авторизация -- проверка прав на получение дампа (администратор)
$Sql = "select user_id, user_admin from Users where user_hide = 0 and trim(user_email) = trim('".$_GET['Login']."') and user_password = '".md5(trim($_GET['Password']))."'";
$Result = MySqlQuery($Sql);  
$Row = mysql_fetch_assoc($Result);

if ($Row['user_id'] <= 0) 
{
   print("Autenthication failed");
   return;  
} 

if ($Row['user_admin'] == 0)
{
   print("Authorization failed");
   return;
}

// Сбор данных для дампа
$data = array();

// Raids: raid_id, raid_name, raid_registrationenddate
$Sql = "select raid_id, raid_name, raid_registrationenddate from Raids";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["Raids"][] = $Row; }

// Distances: distance_id, raid_id, distance_name
$Sql = "select distance_id, raid_id, distance_name from Distances";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["Distances"][] = $Row; }

// Levels: level_id, distance_id, level_name, level_order, level_starttype, level_pointnames, level_pointpenalties, level_begtime, level_maxbegtime, level_minendtime, level_endtime
$Sql = "select level_id, distance_id, level_name, level_order, level_starttype, level_pointnames, level_pointpenalties, level_begtime, level_maxbegtime, level_minendtime, level_endtime from Levels";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["Levels"][] = $Row; }

// LevelPoints: levelpoint_id, level_id, pointtype_id
$Sql = "select levelpoint_id, level_id, pointtype_id from LevelPoints";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["LevelPoints"][] = $Row; }

// Teams: team_id, distance_id, team_name, team_num
$Sql = "select team_id, distance_id, team_name, team_num from Teams";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["Teams"][] = $Row; }

// Users: user_id, user_name, user_birthyear
$Sql = "select user_id, user_name, user_birthyear from Users";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["Users"][] = $Row; }

// TeamUsers: teamuser_id, team_id, user_id, teamuser_hide
$Sql = "select teamuser_id, team_id, user_id, teamuser_hide from TeamUsers";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["TeamUsers"][] = $Row; }

// TeamLevelDismiss: user_id, levelpoint_id, team_id, teamuser_id, teamleveldismiss_date, device_id
$Sql = "select user_id, levelpoint_id, team_id, teamuser_id, teamleveldismiss_date, device_id from TeamLevelDismiss";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["TeamLevelDismiss"][] = $Row; }

// TeamLevelPoints: user_id, levelpoint_id, team_id, teamlevelpoint_date, device_id, teamlevelpoint_datetime, teamlevelpoint_points, teamlevelpoint_comment
$Sql = "select user_id, levelpoint_id, team_id, teamlevelpoint_date, device_id, teamlevelpoint_datetime, teamlevelpoint_points, teamlevelpoint_comment from TeamLevelPoints";
$Result = MySqlQuery($Sql);
while ( ( $Row = mysql_fetch_assoc($Result) ) ) { $data["TeamLevelPoints"][] = $Row; }

// Вывод json
print json_encode( $data );

?>
